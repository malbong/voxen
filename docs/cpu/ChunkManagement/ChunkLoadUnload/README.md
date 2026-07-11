# Chunk Load / Unload

## 1. 개요

Load와 Unload는 [ChunkManager](../ChunkManager/README.md) `Update()` 파이프라인에서 청크 생명주기의 시작과 끝을 담당하는 두 짝지어진 시스템이다. 카메라 이동에 반응해 필요한 청크는 Pool에서 꺼내 비동기로 초기화하고, 필요 없어진 청크는 Pool로 되돌려 자원을 재사용한다.

파이프라인 안에서 이 두 시스템은 다음 4개 함수로 구성된다.

```
Update()
├─ UpdateLoadUnLoadChunkList     로드/언로드 대상 결정 (트리거 기반)
├─ LoadChunks                    로드 비동기 디스패치
├─ SyncLoadedChunks              로드 완료 수확 + GPU 업로드 + Patch 큐 연결
└─ UnloadChunks                  범위 밖 청크 정리
```

이 문서는 각 단계가 언제 어떤 조건으로 실행되며, 어떤 자료구조를 통해 상호작용하는지 정리한다.

## 2. 언제 로드/언로드 대상이 재계산되는가

`UpdateLoadUnLoadChunkList`는 매 프레임 호출되지 않는다. **카메라가 청크 경계를 넘은 프레임에만** 실행된다.

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

`m_isOnChunkUpdateDirtyFlag`는 `OnChunkUpdateDirtyFlag()` 인라인 setter로 세워지며, 이 setter는 카메라가 자신의 `chunkPosition`이 바뀌었음을 감지했을 때 호출한다. 청크 좌표가 그대로면 로드/언로드 대상 집합이 변하지 않으므로 8000개가 넘는 위치를 재순회할 이유가 없다.

### 2.1 UpdateLoadUnLoadChunkList의 역할

세 자료구조를 재구성한다.

```
[1] m_renderablePosMap    = 카메라 중심 CHUNK_COUNT × CHUNK_COUNT × MAX_HEIGHT_CHUNK_COUNT 좌표 집합
[2] m_waitLoadChunkPosMap = m_renderablePosMap 중 아직 m_chunkMap에 없는 좌표들
[3] m_unloadChunkList     = m_chunkMap 중 m_renderablePosMap에 없는 청크들
```

- `m_renderablePosMap` — "이번 카메라 위치에서 살아있어야 할 청크 좌표"의 전체 집합. 이후 프레임에서 로드 완료 시 유효성 검사(§4.2), 언로드 판정 등에 모두 재사용된다.
- `m_waitLoadChunkPosMap` — 로드 대기 큐. 아직 초기화가 시작되지 않은 새 좌표만.
- `m_unloadChunkList` — 언로드 대기 큐. 이번 프레임 안에서 정리될 청크들.

이 함수는 대상만 **표시**하고 실제 로드/언로드는 하지 않는다. 실행은 아래 3~4장이 담당한다.

## 3. LoadChunks — 비동기 디스패치

`LoadChunks`는 매 프레임 호출되며, 현재 워커 슬롯이 여유 있을 때만 새 작업을 발사한다.

```cpp
void ChunkManager::LoadChunks(Camera& camera)
{
    if (m_initFutures.size() == m_initThreadCount)
        return;                                // 슬롯 꽉 참 → 이 프레임엔 발사 안 함

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

이건 완전한 우선순위 큐가 아니라 "한 프레임 단위 정렬"이다. 다음 프레임에서 카메라가 움직였다면 정렬도 다시 반영된다. 프레임당 정렬 비용과 우선순위 정확도 사이의 실용적 타협이다.

### 3.2 대여 실패 시 롤백

Chunk Pool과 ChunkLoadMemory Pool은 각각 독립된 풀이다. Chunk는 대여했는데 Memory가 없다면 워커에 넘길 수 없으므로 Chunk도 즉시 되돌린다. 이 실패는 조용히 넘어가고 다음 프레임에 재시도된다 — 로드가 실패한 것이 아니라 **연기**된 것이다.

풀 고갈 자체는 정상적인 백프레셔 신호다. 프레임 파이프라인이 스스로 부하를 억제하는 흐름이라 별도 예외 처리가 필요 없다.

### 3.3 std::async와 스레드 풀

`std::async(std::launch::async, ...)`가 매번 새 스레드를 만들어 비용이 크지 않을까? MSVC 표준 라이브러리에서는 이 호출이 **시스템 스레드 풀(Windows Thread Pool)**에서 실행된다 — 표준이 강제하는 사항은 아니지만 MSVC 구현에서는 Concurrency Runtime 위에서 풀 기반으로 스케줄된다. 매번 `CreateThread`가 호출되지 않는다는 뜻이다.

거리 기반 우선순위 큐, 잡 취소 등이 필요해지기 전까지는 이 구성으로 충분하다는 판단이다.

## 4. SyncLoadedChunks — 완료 수확과 GPU 업로드

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

**(c) UpdateCpuBufferCount의 이유**  
Chunk의 CPU vector `size()`는 워커 스레드에서 채워졌다. GPU 업로드는 vector 데이터 자체를 참조하지만, 렌더 스레드가 `DrawIndexed`에 넣는 인덱스 개수는 원자적이어야 한다. `UpdateCpuBufferCount`는 `vertexCount`/`indexCount`를 vector `size()`에서 스냅샷하여, 렌더 스레드가 vector 자체를 만지지 않고도 정확한 개수를 알게 한다.

**(d) GPU 업로드는 항상 메인 스레드**  
DX11 Immediate Context는 단일 스레드 제약이 있다. 워커에서 GPU 리소스를 만지면 미정의 동작이 발생한다. Sync 단계는 메인 스레드에서 실행되므로 안전하다.

## 5. 로드가 Patch에 미치는 영향

SyncLoadedChunks (e)에서 호출되는 `UpdatePatchChunkMap(chunk, loadPatchResult)`이 이 문서의 핵심 접점이다. 이 호출로 새로 로드된 청크가 Patch 시스템의 큐와 의존성 맵을 채운다. 로드는 단순히 "이 청크가 준비됨"으로 끝나지 않고, **인접 청크와 자기 자신의 후속 패치까지 예약**하는 단계다.

Patch 시스템으로 유입되는 3개 경로:

1. **역방향 조회 — 이미 로드된 이웃이 나를 기다리고 있었나?**  
   `m_lookupDependencySet[curPos]`가 존재하면, 이전에 다른 청크(예: 이웃)가 초기화되며 나를 대상으로 만든 `PatchData`가 `m_patchDependencyMap`에 남아 있다는 뜻이다. 그 데이터를 꺼내 `m_waitPatchChunkMap[curPos]`에 밀어 넣는다. 자기 자신의 후속 패치 예약.

2. **플레이어 변경 이력 재적용**  
   `m_cameraPatchChunkMap[curPos]`는 이 청크가 언로드-재로드되기 전에 플레이어가 만든 블록 변경을 영구 보관한 곳이다. 로드 완료 시 이 데이터도 `m_waitPatchChunkMap[curPos]`에 재주입되어, 재로드 후 세계가 이전 상태로 복원된다.

3. **정방향 전파 — 내가 이웃에게 남길 것**  
   `loadPatchResult`에는 나의 초기화 중 발생한 크로스 청크 패치가 담겨 있다 (예: 나무가 이웃 청크로 뻗어감). 두 가지로 처리한다.
   - `m_patchDependencyMap[curPos][neighborPos]`와 `m_lookupDependencySet[neighborPos]`에 항상 기록 (이웃이 아직 로드 안 되었더라도 미래 로드 시점에 참조되도록).
   - 이웃이 이미 로드되어 있고 아직 나로부터 패치받은 적 없으면 (`m_patchedChunkSet` 검사) 즉시 `m_waitPatchChunkMap[neighborPos]`에 밀어 넣는다.

이 세 경로의 자료구조 상세와 의도는 [Patch](../Patch/README.md) 문서에서 다룬다. 여기서는 **"로드 완료가 Patch 큐를 채우고, 지연된 의존성을 청산한다"**는 흐름만 잡아두면 된다.

## 6. UnloadChunks — 정리, 단 GPU 버퍼는 유지

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

역참조 정리가 특히 중요하다. `m_lookupDependencySet[destPos]`에 `chunkPos`가 남아 있으면, 나중에 `destPos` 청크가 새로 로드될 때 존재하지 않는 나(`chunkPos`)를 조회하려 시도한다. 이 정리를 빠뜨리면 로드 시 패치 데이터가 헛되이 사라지거나(비교적 양호) stale 포인터를 참조할 수도 있다.

### 6.2 chunk->Clear()

`ReleaseChunkToPool` 안에서 `chunk->Clear()`가 호출된다. Clear가 하는 일은 [ChunkStructure §7](../ChunkStructure/README.md)에 정리되어 있다. 요약하면:

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

이 재사용 전략의 세부는 [ChunkManager §5](../ChunkManager/README.md)에 있다.

### 6.4 m_cameraPatchChunkMap은 왜 정리 안 하는가

`m_cameraPatchChunkMap`은 플레이어가 만든 블록 변경(파괴/추가)을 영구 보관하는 맵이다. Unload 정리 대상에서 명시적으로 **제외**된다. 언로드 후 다시 시야에 들어와 재로드될 때 SyncLoadedChunks (e)의 §5.2 경로로 되살아나야 하기 때문이다. 청크 라이프사이클과 플레이어 세계 변경 이력은 서로 다른 축의 데이터다.

## 7. Load와 Unload의 프레임 내 순서

`Update()`에서 두 시스템은 다음 순서로 배치된다.

```
UpdateLoadUnLoadChunkList   (dirty flag 시)
LoadChunks
SyncLoadedChunks            ← 여기서 즉시 언로드 판정을 하면 m_unloadChunkList 에 밀어 넣음
UnloadChunks                ← 이번 프레임에 함께 정리됨
PatchChunks
SyncPatchedChunks           ← 여기서도 유효성 검사 후 범위 밖이면 그냥 continue (별도 unload 안 함)
```

Sync 두 곳 모두에서 유효성 검사를 수행하는 방식이 살짝 다르다.

- **SyncLoadedChunks**: 범위 밖 청크를 발견하면 `m_unloadChunkList`에 밀어 넣는다. 초기화 자원이 이미 소모되었기 때문에 GPU 업로드는 스킵하되, 리소스 정리는 반드시 필요.
- **SyncPatchedChunks**: 범위 밖 청크를 발견하면 그냥 `continue`. 청크는 이미 언로드 흐름을 탔거나 곧 탈 예정이므로 별도 조치 불필요.

## 8. 회고

- **UpdateLoadUnLoadChunkList가 dirty flag 시점에 전체 재구성**을 한다. `CHUNK_COUNT² × MAX_HEIGHT_CHUNK_COUNT` (현재 8192 지점) 순회이므로 청크 경계를 넘는 프레임에서 눈에 띄는 스파이크가 발생할 여지가 있다. 이전 프레임 결과와의 차집합 계산으로 개선 가능.
- **거리 기반 정렬이 프레임당 O(N log N)** 이다. 우선순위 큐로 유지하면 매 프레임 정렬을 피할 수 있다. 다만 카메라가 매 프레임 조금씩 움직이므로 정렬 결과가 크게 바뀌지 않아 실용상 큰 문제는 아니다.
- **풀 고갈 시 조용한 지연**이 안전 백프레셔로 작동하지만, 어느 풀에서 병목이 생겼는지 관측이 어렵다. 로그·카운터를 추가하면 프로파일링에 도움된다.
- **`m_lookupDependencySet` 역참조 정리 로직**은 조건 분기가 중첩되어 있어 실수가 나기 쉬운 지점이다. 자료구조 자체를 양방향 그래프로 캡슐화하면 각 삽입/삭제 지점의 코드 복잡도가 줄어든다.
