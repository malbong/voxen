# Chunk Load / Unload

<img width="600" height="420" alt="Image" src="https://github.com/user-attachments/assets/fe867b1d-e9c6-4d90-8e52-181aa1779652" />

## 1. 개요

Load와 Unload는 [ChunkManager](../ChunkManager/README.md) `Update()` 파이프라인에서 Chunk의 생명주기 시작과 끝을 담당하는 부분이다.

카메라 이동에 반응해 필요한 청크는 Pool에서 꺼내 비동기로 초기화하고, 필요 없어진 청크는 Pool로 되돌려 자원을 재사용한다.

파이프라인 안에서 이 두 시스템은 다음 4개 함수로 구성된다.

```
Update()
├───── UpdateLoadUnLoadChunkList     로드/언로드 대상 결정 (트리거 기반)
├─ LoadChunks                    로드 비동기 디스패치
├─ SyncLoadedChunks              로드 완료 동기화 -> GPU 업로드 + Patch 큐 연결
└─ UnloadChunks                  범위 밖 청크 정리
```

## 2. 언제 로드/언로드 대상이 재계산되는가

`UpdateLoadUnLoadChunkList` 호출 시에 대상은 결정된다.

- 이는 매 프레임 호출되지 않는다. **카메라가 청크 경계(32)를 넘은 프레임에만** 실행된다.
- 카메라가 렌더링할 범위를 기준으로 데이터를 재구성하므로, 청크 경계 내부에 그대로 존재하는 경우 트리거가 활성화 되지 않는다.

```cpp
void ChunkManager::Update(float dt, Camera& camera, const Light& light)
{
    if (m_isOnChunkUpdateDirtyFlag) {
        UpdateLoadUnLoadChunkList(camera.GetChunkPosition());
        m_isOnChunkUpdateDirtyFlag = false;
    }
    ...
}
```

### 2.1 UpdateLoadUnLoadChunkList의 역할

3개의 데이터를 구성한다.

```
[1] m_renderablePosMap    = 카메라 중심 CHUNK_COUNT × CHUNK_COUNT × MAX_HEIGHT_CHUNK_COUNT 좌표 집합
[2] m_waitLoadChunkPosMap = m_renderablePosMap 중 아직 m_chunkMap에 없는 좌표들
[3] m_unloadChunkList     = m_chunkMap 중 m_renderablePosMap에 없는 청크들
```

- `m_renderablePosMap` — "이번 카메라 위치에서 살아있어야 할 청크 좌표"의 전체 집합. 이후 프레임에서 로드 완료 시 유효성 검사(§4.2), 언로드 판정 등에 모두 재사용된다.
- `m_waitLoadChunkPosMap` — 로드 대기 큐. 아직 초기화가 시작되지 않은 새 좌표만.
- `m_unloadChunkList` — 언로드 대기 큐. 무결한 청크 중 거리가 멀어서 정리될 청크들의 리스트.
- cf. `m_chunkMap[pos] = chunk` — 로드 및 동기화가 완료된 렌더링이 가능해진 무결한 청크를 담는 컨테이너다.

이 함수는 대상만 **표시**하는 개념이지, 실제 로드/언로드는 하지 않는다. 실행은 아래 3~4장이 담당한다.

## 3. LoadChunks — 비동기 디스패치

`LoadChunks`는 매 프레임 호출되며, 현재 워커 슬롯이 여유 있을 때만 새 작업을 만들어 시작한다.

```cpp
void ChunkManager::LoadChunks(Camera& camera)
{
    if (m_initFutures.size() == m_initThreadCount)
        return;                                // 슬롯 꽉 참 → 실행하지 않음

    m_waitLoadChunkPosList.clear();
    for (auto& p : m_waitLoadChunkPosMap) m_waitLoadChunkPosList.push_back(p.first);
    SortPosListByCameraDistance(camera.GetPosition(), m_waitLoadChunkPosList);
    // 정렬: 카메라에서 먼 것이 앞, 가까운 것이 뒤 → pop_back으로 가까운 것부터 소비

    while (!m_waitLoadChunkPosList.empty() && m_initFutures.size() < m_initThreadCount) {
        Chunk* chunk = GetChunkFromPool();
        if (!chunk) return;

        ChunkLoadMemory* mem = GetChunkLoadMemoryFromPool();
        if (!mem) { ReleaseChunkToPool(chunk); return; }   // 대여 실패 → 롤백

        const PosInt3& pos = m_waitLoadChunkPosList.back();
        m_initFutures.push_back({chunk,
            std::async(std::launch::async, &Chunk::Initialize, chunk, pos, mem)});
        m_waitLoadChunkPosList.pop_back();
        m_waitLoadChunkPosMap.erase(pos);
    }
}
```

### 3.1 카메라 거리 정렬로 우선순위 시늉

`m_waitLoadChunkPosMap`은 unordered_map이라 순서가 없다. 정렬 없이 처리하면 시야 방향과 무관하게 대각선의 먼 청크가 먼저 로드되는 등 시각적으로 눈에 띈다.

매 프레임 `m_waitLoadChunkPosList`에 옮겨 담고 카메라 거리로 정렬한다. **먼 것이 앞, 가까운 것이 뒤에 오도록** 정렬한 뒤 `pop_back`으로 소비하면 사실상 가까운 청크 우선 스케줄이 된다.

사실 이러한 거리에 따른 작업 우선순위는 잡 시스템에서 결정되어야 한다.

- 해당 프로젝트에서는 단순히 쓰레드를 실행시킬 우선순위가 정렬되는 것이지, 실제로 작업이 들어간 워커를 정렬하는게 아니다.
- 잡 시스템은 구현의 복잡도가 크기 때문에 실행 순서만 가까운 청크에 따라 정렬하기로 결정했다.

### 3.2 대여 실패 시 롤백 -> 실패가 아닌 연기(다음 프레임에 재시도가 있을 것)

Chunk Pool과 ChunkLoadMemory Pool은 각각 독립된 풀이다. Chunk는 대여했는데 Memory가 없다면 워커에 넘길 수 없으므로 Chunk도 즉시 되돌린다. - 이 실패는 조용히 넘어가고 다음 프레임에 재시도된다 — 이는 로드가 실패한 것이 아니라 **연기**된 것이다.

또한 두번째 ChunkLoadMemory가 풀 대여 시 실패할 때 이전에 받은 Chunk 메모리는 다시 반환한다.

- 이는 Pool의 누수를 막기 위함인데, 사실 풀 자체를 객체로 구성하고 생성자/소멸자에서 RAII를 충분히 할 수 있었지만, 누수의 경우가 한정되었다고 판단하여 위와 같이 진행했다.
- 더욱 안전한 시스템을 구성하려면 반드시 RAII 원칙으로 생성 및 초기화하는 편이 나아보인다.

### 3.3 std::async와 스레드 풀

쓰레드 풀 없이 `std::async(std::launch::async, ...)`가 매번 새 스레드를 만들어 비용이 크지 않을까?

- MSVC 표준 라이브러리에서는 이 호출이 **시스템 스레드 풀(Windows Thread Pool)**에서 실행된다.
- 표준이 강제하는 사항은 아니지만 MSVC 구현에서는 Concurrency Runtime 위에서 풀 기반으로 스케줄된다.
- 매번 `CreateThread`가 호출되지 않는다는 뜻이다. (Claude Code - Sonet5 참고한 내용입니다.)

## 4. SyncLoadedChunks — 완료 동기화 및 GPU 업로드 -> 상태 변경

디스패치된 future를 매 프레임 확인하고, 완료된 것만 후처리한다.

```cpp
for (auto it = m_initFutures.begin(); it != m_initFutures.end(); ) {
    if (it->second.wait_for(0us) != std::future_status::ready) { ++it; continue; }

    Chunk* chunk = it->first;
    ChunkLoadMemory* mem = it->second.get();
    it = m_initFutures.erase(it);

    // memory 반환 전, loadPatchResult만 move로 뽑아둠
    PosHashMap<PatchDataHashSet> loadPatchResult(std::move(mem->loadPatchResult));
    ReleaseChunkLoadMemoryToPool(mem);

    ...  // §4.2 이하
}
```

`wait_for(0us)`는 논블로킹이라 완료되지 않은 future는 다음 프레임으로 넘긴다. Sync 함수 자체가 프레임을 붙잡지 않는다.

### 4.1 loadPatchResult만 move로 회수

`ChunkLoadMemory`는 다시 풀로 돌아가 재사용되어야 하지만, 그 안의 `loadPatchResult`(청크 초기화 중 발생한 크로스 청크 패치 데이터)는 이후 Patch 큐 연결에 계속 필요하다. Memory를 반환하기 전 `std::move`로 뽑아 지역 변수로 이동시킨다. 대용량 해시맵의 깊은 복사를 피한다.

### 4.2 완료된 청크의 처리 순서

```cpp
const PosInt3 pos = Utils::VectorToPosInt3(chunk->GetOffsetPosition());

// (a) 아직 유효한 위치인가?
if (m_renderablePosMap.find(pos) == m_renderablePosMap.end()) {
    m_unloadChunkList.push_back(chunk);
    continue;
}

// (b) 청크 상태 세팅
chunk->SetUpdateRequired(true);       // 등장 애니메이션 시작
chunk->SetLoad(true);                 // 로드 완료 상태 전환

// (c) 멀티스레드 안전성 확보
chunk->UpdateCpuBufferCount();

// (d) GPU 업로드 (메인 스레드 단독)
UpdateChunkGPUBuffer(chunk);

// (e) 로드된 청크가 Patch 시스템에 진입
UpdatePatchChunkMap(chunk, loadPatchResult);

// (f) 활성 청크 맵에 등록
m_chunkMap[pos] = chunk;
```

**(a) 즉시 언로드 경로**  
로드가 시작된 후 카메라가 크게 움직여, 완료 시점엔 이 청크가 이미 범위 밖일 수 있다. 유효성 검사를 통과하지 못하면 GPU 업로드 없이 곧장 `m_unloadChunkList`로 밀어 넣는다. 이렇게 하면 이번 프레임 `UnloadChunks`에서 함께 정리된다.

- 단순히 다음 프레임에 언로드 시키면 되지만, GPU 버퍼를 업데이트 하는 비용을 아끼기 위해 추가 검사를 진행하게 되었다.
- 추가적으로 `m_chunkMap`에는 렌더링 거리에 존재하고, 로드가 완료되어 동기화된 무결한 청크만 들어가게되는 효과도 부수적으로 얻었다.

**(c) UpdateCpuBufferCount의 이유**  
Patch를 구현하기 전 코드에서는 `UpdateCpuBufferCount의`는 존재하지 않았다.
단순히 `context->DrawIndexed(Chunk->cpuIndices.size()..)`로 렌더링해도 무방했지만 Patch가 도입되고 나서 1frame Flickering이 존재했다.

Patch를 진행하는 청크는 매 프레임 렌더링 되어야했는데, Patch 도중 cpu 컨테이너의 데이터 크기가 바뀌어 레이스컨디션 문제로 UB가 나타난 것이다.
그래서 Chunk가 Patch 중이더라도 원자적으로 렌더링을 계속하기 위해 별도의 `cpuIndicesSize` 변수를 두고 로드 및 패치가 끝난 후 Update하여
`context->DrawIndexed(Chunk->cpuIndicesSize)`로 렌더링하여 문제를 해결했다.

**(d) GPU 업로드는 항상 메인 스레드**  
DX11 Immediate Context는 단일 스레드 제약이 있다. 워커에서 GPU 리소스를 만지면 미정의 동작이 발생한다. Sync 단계는 메인 스레드에서 실행되므로 안전하다.

**(e) 아래 5번에서 자세히**

**(f) m_chunk는 항상 무결함**
m_chunkMap에 Chunk를 삽입하는 곳은 이곳이 전부다.
로딩이 완료되고, 범위 내부에 존재하고, 상태가 적절하고, GPU가 업로드 되어야 m_chunkMap에 들어오게 되는 것이다.
하위 코드에서 m_chunkMap의 무결함을 검증하지 않아도 되지만, 의심스러워 추가 검사하는 로직이 존재하긴 한다.
`if (!m_chunkMap[pos]->IsLoaded())`

## 5. (e) Load가 Patch에 미치는 영향

SyncLoadedChunks (e)에서 호출되는 `UpdatePatchChunkMap(chunk, loadPatchResult)`이 Load->Patch의 핵심 접점이다. 이 호출로 새로 로드된 청크가 Patch 시스템의 큐와 의존성 맵을 채운다. 로드는 단순히 "이 청크가 준비됨"으로 끝나지 않고, **인접 청크와 자기 자신의 후속 패치까지 예약**하는 단계다.

```cpp
m_patchDependencyMap: 내가 영향을 미치는 청크와 패치정보를 담는 의존성 데이터
- m_patchDependencyMap[주체] = { { 객체1: {패치1}, {패치2}, ..}, { 객체2: {패치1}, ..}, ..}
- "나는 누구를 어떻게 패치해야 하는가?"

m_lookupDependencySet: 의존성을 찾는 LUT
- m_lookupDependencySet[객체] = { 주체1, 주체2, 주체3 }
- "나를 패치하라고 하는 다른 청크들이 있는가?"
- 패치를 하지 않아도 데이터 담아 차후에 사용: 아래의 m_patchedChunkSet과 성격이 조금 다름
 -> 다른 객체가 로드되든 말든 가지고 있는 데이터임

m_patchedChunkSet: patch 리스트에 등록된 정보를 담음, re Patch 방지
- m_patchedChunkSet[객체] = { 주체1, 주체2, }
- "나는 누구에 의해 패치를 했는가?"
- 패치 리스트에 넣은 경우에만 데이터 담아 사용함: 위의 m_lookupDependencySet와 성격이 조금 다름
 -> 객체가 로드되어 패치 리스트에 들어간 경우임

m_waitPatchChunkMap: patch할 청크들을 예약 목록에 담음
- m_waitPatchChunkMap[주체] = { 패치1, 패치2, .. }
```

Patch의 상세와 의도는 [ChunkPatch](../ChunkPatch/README.md) 문서에서 다룬다. 여기서는 **"로드 완료가 Patch 큐를 채우고, 지연된 의존성을 처리한다"**는 흐름만 잡아두면 된다.

## 6. UnloadChunks — CPU 정리, 단 GPU 버퍼는 유지

```cpp
while (!m_unloadChunkList.empty()) {
    Chunk* chunk = m_unloadChunkList.back();
    m_unloadChunkList.pop_back();

    PosInt3 chunkPos = Utils::VectorToPosInt3(chunk->GetOffsetPosition());

    m_chunkMap.erase(chunkPos);
    m_waitPatchChunkMap.erase(chunkPos);
    m_patchedChunkSet.erase(chunkPos);

    // patchDependencyMap 정리 + 역참조(lookupDependencySet) 정리
    if (m_patchDependencyMap.count(chunkPos)) {
        for (const auto& [destPos, _] : m_patchDependencyMap[chunkPos]) {
            if (m_lookupDependencySet.count(destPos)) {
                m_lookupDependencySet[destPos].erase(chunkPos);
                if (m_lookupDependencySet[destPos].empty())
                    m_lookupDependencySet.erase(destPos);
            }
        }
        m_patchDependencyMap.erase(chunkPos);
    }

    ReleaseChunkToPool(chunk);   // 내부에서 chunk->Clear()
}
```

### 6.1 정리 대상

- `m_chunkMap` — 활성 청크 맵에서 제거
- `m_waitPatchChunkMap` — 아직 실행 안 된 이 청크의 패치 요청 제거
- `m_patchedChunkSet` — 이 청크가 받았던 패치 이력 제거
- `m_patchDependencyMap` — 이 청크가 이웃에게 남긴 패치 데이터 제거
- `m_lookupDependencySet` — 위 정리와 짝지어 **역참조도 함께 청소**

역참조 정리가 특히 중요하다. `m_lookupDependencySet[destPos]`에 `chunkPos`가 남아 있으면, 나중에 `destPos` 청크가 새로 로드될 때 존재하지 않는 나(`chunkPos`)를 조회하려 시도한다.

### 6.2 chunk->Clear()

`ReleaseChunkToPool` 안에서 `chunk->Clear()`가 호출된다.

- 상태 플래그 리셋 (`isLoaded`, `isPatching`, `isUpdateRequired`, `onPatchDirtyFlag`)
- `m_instanceMap.clear()`
- CPU 메시 vector 4종 `clear()` (capacity는 유지)
- `UpdateCpuBufferCount()`로 count도 0으로 스냅샷

`m_blocks` 배열 자체는 초기화하지 않는다. 다음 Initialize에서 `CHUNK_SIZE_P³` 전 지점을 덮어쓰기 때문이다.

### 6.3 GPU 버퍼는 왜 클리어하지 않는가

이 시스템의 핵심 설계 결정이다. `ChunkManager`가 소유한 GPU 버퍼(`m_opaqueVertexBuffers[id]` 등)는 **Unload 시점에 손대지 않는다.**

**이유 1 — 재활용 슬롯이 재활용 버퍼가 된다.**  
Chunk의 `m_id`는 Pool 슬롯 인덱스이고, 이 ID는 청크가 언로드되어 Pool에 돌아가도 다음 대여자에게 그대로 넘어간다. 즉 같은 슬롯을 여러 청크가 시간차로 공유한다. 이때 대응하는 GPU 버퍼도 함께 공유된다.

**이유 2 — CreateBuffer는 비싸고, UpdateBuffer는 싸다.**  
D3D11의 `CreateBuffer`는 드라이버 커널 진입을 수반한다. 반면 이미 존재하는 버퍼에 `Map`/`Unmap`으로 데이터만 덮어쓰는 `UpdateBuffer`는 훨씬 저렴하다. 언로드 시 버퍼를 `Release`했다가 재로드 시 `Create`하는 것보다, 유지하고 재사용하는 편이 낫다.

**이유 3 — 슬롯 크기가 자연스럽게 안정화된다.**  
같은 슬롯을 여러 청크가 지나가면서, 버퍼 크기는 점차 "그 슬롯을 지나간 가장 큰 청크"의 크기로 수렴한다. 이후엔 대부분 `UpdateBuffer`만 발생하고 `ResizeBuffer`(재생성)는 거의 트리거되지 않는다. Voxen 실행 초반 몇 초 이후로는 재생성 빈도가 크게 떨어진다.

### 6.4 m_cameraPatchChunkMap은 왜 정리 안 하는가

`m_cameraPatchChunkMap`은 플레이어가 만든 블록 변경(파괴/추가)을 영구 보관하는 맵이다. Unload 정리 대상에서 명시적으로 **제외**된다. 언로드 후 다시 시야에 들어와 재로드될 때 SyncLoadedChunks (e)의 경로로 되살아나야 하기 때문이다. 청크 라이프사이클과 플레이어 세계 변경 이력은 서로 다른 축의 데이터다.

언젠가 클리어해야되지만, 상용 엔진이나 게임이 아니기때문에 그냥 두었다.

## 7. 회고

- Load 멀티쓰레드와 Unload에는 어떤 청크가 들어가야하는지 등 모든 상황에 stable 하게 구성하기 위해 고생했던 챕터
  - 카메라가 아무리 빨리 움직여도 필요한 청크를 모두 로드 및 렌더링할 수 있게 끔 로직을 구성해야 했다.
  - Load된 데이터만 Unload를 해야하는지, 아니면 일단 후보가 될 청크는 무조건 Unload 할지 등
  - 내가 가진 데이터는 무결한지, 상태 검사는 어디까지 해야하는지 확정하기 너무 어려웠다.
  - 비효율이 존재하더라도, 위에서 하나씩 깨끗한 데이터를 만들었고, 결국 해결해서 뿌듯하다.

- 미흡한점이 많이 보인다.
  - UpdateLoadUnLoadChunkList가 모든 방향 3D 순회로 비효율로 보인다.
    - 적절한 알고리즘이 떠오르지 않는다.
  - 거리 기반 정렬을 매 프레임한다.
    - 트리거 발생 시(`UpdateLoadUnLoadChunkList`)에서 한번 해도 괜찮은 로직이다.
    - 어차피 청크 경계에 나가면 클리어된 후 다시 재구성하기 때문이다.
    - 우선순위큐를 사용해도 좋아보인다.
  - 메모리 풀 고갈 시 조용히 지연되지만, 단순히 안전히 기다리기만하지 다른 메모리를 할당하여 추가 풀을 구성하지는 않는다.
    - 그럴 필요가 존재하면 하면 될 것 같다.
