# Chunk Patch

## 1. 개요

Patch는 이미 로드된 청크의 블록·인스턴스 상태를 **부분 변경**하는 시스템이다. 청크 초기화([ChunkStructure §3](../ChunkStructure/README.md))가 "청크 하나를 처음부터 만드는" 경로라면, Patch는 "이미 존재하는 청크의 일부를 다시 만드는" 경로다.

파이프라인 안에서 Patch는 [ChunkManager](../ChunkManager/README.md) `Update()`의 두 단계 — `PatchChunks`(디스패치), `SyncPatchedChunks`(수확) — 로 실행되며, DDA 기반 [블록 피킹](#8-dda-picking-으로-패치-트리거하기)이 사용자 트리거 지점이다.

## 2. Patch가 필요한 이유

Voxen에는 청크 하나의 초기화만으로 세계를 완성할 수 없는 세 가지 상황이 있다.

**(a) 크로스 청크 오브젝트 — 나무·인스턴스가 청크 경계를 넘음**  
나무 하나는 최대 `TREE_SIZE = 11` 블록의 3D 볼륨을 차지하며, 청크 경계에 걸쳐 자라기 쉽다. 이때 나무의 일부 블록·덩굴 인스턴스는 이웃 청크에 기록되어야 하지만, 나무를 생성한 청크는 이웃 청크의 `m_blocks`를 직접 만질 수 없다 (스레드 안전성, 로드 순서 무관성).  
→ 데이터를 **패치 큐**에 넘겨두고 이웃 청크가 처리하도록 위임한다.

**(b) 플레이어의 세계 조작 — 블록 파괴/추가**  
마우스로 블록을 부수거나 추가할 때, 이미 로드된 청크의 `m_blocks`를 즉시 바꾸고 GPU 버퍼에 반영해야 한다. 이 경로가 Patch의 가장 직접적인 사용 사례다.

**(c) Greedy Meshing이 요구하는 패딩 갱신**  
Chunk의 `m_blocks[34][34][34]`는 좌우 1칸씩 패딩을 갖는다 ([ChunkStructure §2.1](../ChunkStructure/README.md)). 이 패딩에는 인접 청크의 경계 블록 정보가 들어 있어야 Face Culling이 균일하게 동작한다. 나무가 청크 A의 경계에 자라면, 그 블록 정보는 **이웃 청크 B의 패딩**에도 반영되어야 한다.  
→ Patch가 이 패딩 갱신까지 담당한다 ([§6](#6-greedy-meshing-패딩과-경계-전파) 참조).

세 케이스 모두 **"청크는 이미 존재한다, 그리고 그 안의 일부만 바뀌어야 한다"**는 공통 요구를 가진다.

## 3. Chunk::Patch — 워커 스레드에서의 실행

```cpp
ChunkLoadMemory* Chunk::Patch(const PatchDataHashSet& patchDataSet, ChunkLoadMemory* memory)
{
    m_isPatching = true;
    m_onPatchDirtyFlag = false;

    for (const PatchData& p : patchDataSet) {
        int x = p.localX, y = p.localY, z = p.localZ;

        // (a) Instance 패치 — 위치가 투명 블록일 때만
        if (p.instance.GetType() != INSTANCE_NONE
            && Block::IsTransparency(m_blocks[x+1][y+1][z+1].GetType())) {
            if (IsInstanceAt(x, y, z)) m_instanceMap.erase({x,y,z});
            m_instanceMap.insert({{x,y,z}, Instance(p.instance)});
        }

        // (b) Block 패치 — 타입이 바뀌었을 때만
        if (p.block.GetType() != BLOCK_NONE) {
            if (p.block.GetType() != m_blocks[x+1][y+1][z+1].GetType()) {
                m_blocks[x+1][y+1][z+1] = Block(p.block);
                m_onPatchDirtyFlag = true;
            }
            if (IsInstanceAt(x, y, z)) m_instanceMap.erase({x,y,z});
        }
    }

    if (m_onPatchDirtyFlag)
        InitWorldVerticesData(memory);         // ← 전체 재메싱

    return memory;
}
```

핵심은 마지막 줄이다. **블록이 실제로 바뀐 경우에만** (`m_onPatchDirtyFlag == true`) 메시를 재생성한다. 인스턴스만 바뀐 경우는 인스턴스가 별도 렌더 경로를 쓰므로 메시 재생성이 불필요하다.

### 3.1 왜 부분 재생성이 아니라 Greedy Meshing 전체 재실행인가

블록 하나만 바뀌었는데 32³ 범위의 Greedy Meshing을 통째로 다시 돌리는 것은 언뜻 낭비처럼 보인다. 그럼에도 이 선택을 유지한 이유는 **부분 재생성의 복잡도가 지나치게 높기 때문이다.**

Greedy Meshing([BinaryGreedyMeshing 문서](../../MeshOptimization/BinaryGreedyMeshing/README.md))은 4단계로 구성된다.

```
1. 축별 column bit 생성       (블록 타입 분류)
2. Face Cull column bit       (인접 블록과의 관계로 컬링)
3. Face Slice column bit      (같은 타입/방향 슬라이스별로 재구성)
4. Greedy Meshing             (슬라이스 위 사각형 병합)
```

블록 한 칸이 바뀌면 이 4단계에서 다음이 모두 영향을 받는다.

- **1단계**: 그 블록의 x/y/z 세 축 컬럼 3개가 바뀐다.
- **2단계**: 그 블록의 **6면 이웃** 컬럼의 컬링 결과가 바뀐다. 즉 최대 18개 컬럼이 영향권.
- **3단계**: 슬라이스가 재구성된다. 컬럼 하나의 변경이 그 컬럼이 속한 여러 슬라이스에 파급된다.
- **4단계**: 이전에 병합되었던 사각형들이 이제 나뉘거나, 반대로 새로 병합될 수 있다. 병합 결과의 유효성이 슬라이스 국소가 아니라 슬라이스 전체에 걸린다.

부분 재생성은 이 파급을 **정확히 국소화**해야 성립하는데, 특히 4단계의 병합 결과가 슬라이스 전역에 걸린다는 점이 국소화를 실질적으로 무너뜨린다. "이 영역만 다시 계산했더니 이전 사각형이랑 겹친다" 같은 케이스가 산발적으로 발생하고, 이를 정합적으로 다루려는 코드는 원본 Greedy Meshing보다 복잡해진다.

**트레이드오프**:

| 관점 | 전체 재생성 (현재) | 부분 재생성 |
| --- | --- | --- |
| 성능 | 32³ Greedy Meshing 재실행 (수 ms) | 이론상 훨씬 빠름 |
| 구현 복잡도 | Initialize와 동일 코드 재사용 | 별도 전용 로직, 병합 정합성 관리 필요 |
| 유지보수 | 메시 로직 하나만 유지 | 두 로직이 동일 결과를 내야 함 |
| 워커 스레드 병렬화 | 그대로 유효 | 그대로 유효 |

현재 프레임 예산 안에서 전체 재생성이 감내 가능하고, 워커 스레드에서 실행되므로 메인 프레임을 직접 막지 않는다. 이 균형이 무너지지 않는 한 단순함을 유지한다.

## 4. ChunkManager의 Patch 스케줄링

Load와 마찬가지로 Patch도 **디스패치(`PatchChunks`)**와 **수확(`SyncPatchedChunks`)**의 두 함수로 나뉜다. Load/Sync 분리 근거는 [ChunkManager §6.1](../ChunkManager/README.md)과 같다.

### 4.1 PatchChunks — 디스패치와 유효성 검사

```cpp
void ChunkManager::PatchChunks(Camera& camera)
{
    if (m_patchFutures.size() == m_patchThreadCount) return;

    m_waitPatchChunkPosList.clear();
    for (auto& p : m_waitPatchChunkMap) m_waitPatchChunkPosList.push_back(p.first);
    SortPosListByCameraDistance(camera.GetPosition(), m_waitPatchChunkPosList);

    for (auto& chunkPos : m_waitPatchChunkPosList) {
        if (m_patchFutures.size() == m_patchThreadCount) break;

        if (m_chunkMap.find(chunkPos) == m_chunkMap.end())  continue;   // (a)
        Chunk* chunk = m_chunkMap[chunkPos];
        if (!chunk->IsLoaded())   continue;                              // (b)
        if (chunk->IsPatching())  continue;                              // (c)

        ChunkLoadMemory* mem = GetChunkLoadMemoryFromPool();
        if (!mem) return;                                                // (d)

        m_patchFutures.push_back({chunk,
            std::async(std::launch::async, &Chunk::Patch, chunk,
                       std::move(m_waitPatchChunkMap[chunkPos]), mem)});
        m_waitPatchChunkMap.erase(chunkPos);
    }
}
```

디스패치 전에 청크의 상태를 4가지 항목으로 검사한다.

- **(a) `m_chunkMap` 존재 여부** — 이 청크는 아직 로드되지 않았거나 이미 언로드된 상태. 지금 패치할 수 없다. 하지만 `m_waitPatchChunkMap`에서 지우지 **않는다** — 청크가 나중에 로드되면 [Load 시 Patch 큐로 유입](../ChunkLoadUnload/README.md)되는 경로로 재활용된다.
- **(b) `IsLoaded()`** — 청크는 Pool에서 대여되었지만 `Chunk::Initialize()`가 아직 완료되지 않았다. 이 상태에서 Patch를 발사하면 Initialize와 Patch가 같은 청크에서 동시 실행된다. 절대 안 된다. 스킵하고 다음 프레임에 재시도.
- **(c) `IsPatching()`** — 이 청크는 이미 워커 스레드에서 Patch 실행 중. 두 번째 발사를 금지한다.
- **(d) `ChunkLoadMemory` 풀 고갈** — 대여 실패 시 이번 프레임은 여기서 중단, 다음 프레임에 재시도. 로드 쪽과 동일한 백프레셔 흐름.

발사 시 `m_waitPatchChunkMap[chunkPos]`는 `std::move`로 워커에 넘겨진다. 대용량 해시셋의 깊은 복사를 피하고, 매니저 쪽 큐에서는 항목이 제거된다.

### 4.2 SyncPatchedChunks — 수확과 GPU 업로드

```cpp
for (auto it = m_patchFutures.begin(); it != m_patchFutures.end(); ) {
    if (it->second.wait_for(0us) != std::future_status::ready) { ++it; continue; }

    Chunk* chunk = it->first;
    ChunkLoadMemory* mem = it->second.get();
    it = m_patchFutures.erase(it);
    ReleaseChunkLoadMemoryToPool(mem);

    // 범위 밖 청크는 그냥 continue — 이미 언로드 흐름을 탔거나 곧 탄다
    const PosInt3 pos = Utils::VectorToPosInt3(chunk->GetOffsetPosition());
    if (m_renderablePosMap.find(pos) == m_renderablePosMap.end()) continue;

    chunk->UpdateCpuBufferCount();                    // ← §7 참조

    if (chunk->OnPatchDirtyFlag()) {                  // 블록이 실제로 바뀐 경우만
        UpdateChunkGPUBuffer(chunk);
        chunk->SetOnPatchDirtyFlag(false);
    }
    chunk->SetIsPatching(false);
}
```

핵심 두 지점:

- **`OnPatchDirtyFlag()` 검사** — 인스턴스만 바뀐 경우 GPU 메시 업로드는 스킵. 인스턴스는 매 프레임 `UpdateInstanceInfoList`에서 재수집되어 별도 버퍼로 올라가므로 청크 메시 재업로드 없이도 반영된다.
- **`UpdateCpuBufferCount()` 호출** — 이유는 [§7](#7-1-frame-flickering의-근원-과-getindicessize-스냅샷)에서 설명한다.

## 5. Patch 관련 자료구조

Patch 시스템은 다섯 개의 자료구조로 상호작용한다. 각각의 역할이 겹쳐 보여 헷갈리기 쉬우므로 정의를 명확히 정리한다.

```cpp
PosHashMap<PatchDataHashSet>              m_waitPatchChunkMap;
PosHashMap<PosHashMap<PatchDataHashSet>>  m_patchDependencyMap;
PosHashMap<PosHashSet>                    m_lookupDependencySet;
PosHashMap<PosHashSet>                    m_patchedChunkSet;
PosHashMap<PatchDataHashSet>              m_cameraPatchChunkMap;
```

### 5.1 m_waitPatchChunkMap — 실행 대기 큐

```
m_waitPatchChunkMap[대상] = { PatchData1, PatchData2, ... }
```

이번(또는 다음) 프레임의 `PatchChunks`가 이 맵의 키를 순회해 실제로 발사한다. **여기 담겼다는 건 "이 청크의 이 데이터를 곧 워커에 넘길 것"**이라는 뜻이다.

### 5.2 m_patchDependencyMap — 소스→대상 패치 이력

```
m_patchDependencyMap[소스] = { {대상1: {패치들}}, {대상2: {패치들}}, ... }
```

**소스 청크가 초기화 중 어떤 대상 청크들에게 어떤 패치 데이터를 남겼는지**를 완전히 보관한다. 언로드 시 이 맵을 순회하며 청소하기 위해 원본 형태로 유지된다.

주로 두 용도:
- 언로드 시 청소 기준(무엇을 지워야 하나)
- 로드 시 역방향 조회의 실체 (아래 참조)

### 5.3 m_lookupDependencySet — 역방향 조회 LUT

```
m_lookupDependencySet[대상] = { 소스1, 소스2, ... }
```

**"이 대상 청크를 지목한 소스가 누구누구인가"**의 역방향 인덱스. Load 완료 시점에 이 청크(=대상)가 등장하면, `m_lookupDependencySet[curPos]`로 관련 소스 목록을 즉시 찾을 수 있고, 그 소스들의 `m_patchDependencyMap[src][curPos]`에서 실제 패치 데이터를 꺼내온다.

`m_patchDependencyMap`만으로도 역조회는 가능하지만 매번 전체 순회가 필요하다. 이 LUT가 그걸 O(1) 조회로 만든다.

### 5.4 m_patchedChunkSet — 중복 적용 방지

```
m_patchedChunkSet[대상] = { 이미 적용한 소스1, 이미 적용한 소스2, ... }
```

**"이 대상 청크가 어떤 소스로부터의 패치를 이미 실행했는가"**의 이력. `m_lookupDependencySet`과 키/값 구조는 같지만 의미가 다르다.

| 자료구조 | 의미 |
| --- | --- |
| `m_lookupDependencySet[dst]` | dst를 지목한 모든 소스 (실행 여부 무관) |
| `m_patchedChunkSet[dst]` | dst가 이미 실행한 소스 (=적용 완료) |

두 집합의 차집합이 곧 "이 청크가 아직 미적용된 소스"다. 정방향 전파 경로에서 이 차집합을 이용해 이중 발사를 방지한다.

### 5.5 m_cameraPatchChunkMap — 플레이어 패치 영구 보관

```
m_cameraPatchChunkMap[대상] = { 플레이어가 만든 PatchData들 }
```

플레이어의 블록 파괴/추가는 시스템 청크가 아니라 세계의 상태다. 청크가 언로드되어도 이 정보는 지워지지 않고, 재로드 시 다시 적용된다. **청크 라이프사이클과 독립된 영속 데이터**.

Unload 정리 대상에서 명시적으로 제외된다 ([ChunkLoadUnload §6.4](../ChunkLoadUnload/README.md)).

### 5.6 왜 이렇게 많은가

Voxen은 **비동기 로드 순서에 의존하지 않는 세계 상태 일관성**을 목표로 한다. 청크 A와 B가 어떤 순서로 로드되든, A가 B에게 남긴 패치는 B가 준비되었을 때 반영되어야 한다. 이 요구를 정면으로 해결하려면:

- 소스→대상 방향의 이력 저장 (`m_patchDependencyMap`)
- 대상 관점에서의 빠른 역조회 (`m_lookupDependencySet`)
- 이미 적용된 소스 추적 (`m_patchedChunkSet`)
- 지금 워커에게 넘길 큐 (`m_waitPatchChunkMap`)
- 청크 라이프사이클과 분리된 영속 저장 (`m_cameraPatchChunkMap`)

가 각각 별도 자료구조로 필요하다. 다섯 개는 중복이 아니라 서로 다른 축의 정보이다.

## 6. Greedy Meshing 패딩과 경계 전파

Chunk의 `m_blocks[34][34][34]`는 좌우 1칸씩 패딩을 갖고, 이 패딩에는 **인접 청크의 경계 블록 정보**가 들어가야 Face Culling이 청크 경계에서 잘못 판정하지 않는다. 패치가 청크 경계 블록을 만지면, 그 블록 정보는 이웃 청크의 패딩에도 반영되어야 한다.

### 6.1 좌표 변환의 핵심

로컬 좌표가 `0`인 블록은 **왼쪽 이웃 청크의 좌표 `32` (=패딩 슬롯 33)**에 해당한다. 마찬가지로 로컬 좌표 `31`인 블록은 **오른쪽 이웃 청크의 좌표 `-1` (=패딩 슬롯 0)**이다.

```cpp
void ChunkManager::GenerateEdgePatchEntry(int x, int y, int z, ...)
{
    int xEdgeDir = (x == 0) ? -1 : ((x == CHUNK_SIZE - 1) ? 1 : 0);
    if (xEdgeDir) {
        int newX = (xEdgeDir == -1) ? CHUNK_SIZE : -1;   // 32 또는 -1
        // 이웃 청크 오프셋과 함께 PatchData 생성
    }
    // y, z도 동일 로직
}
```

x/y/z 세 축 모두 이 검사를 통과할 수 있으므로 최대 3개의 이웃 청크에 대해 엔트리가 만들어진다 (모서리에 있는 블록의 경우).

### 6.2 PropagatePatchByEdgeBlock — 전파 실행

```cpp
void ChunkManager::PropagatePatchByEdgeBlock(
    Vector3 localPosition, Vector3 chunkOffsetPos, BLOCK_TYPE blockType)
{
    std::pair<PosInt3, PatchData> outEdgePatchEntry[3];
    int count = 0;
    GenerateEdgePatchEntry(localPosition, chunkOffsetPos, blockType, outEdgePatchEntry, count);

    for (int i = 0; i < count; ++i) {
        auto& [neighborPos, patchData] = outEdgePatchEntry[i];

        m_cameraPatchChunkMap[neighborPos].insert(patchData);              // 영구 저장
        if (m_chunkMap.count(neighborPos) && m_chunkMap[neighborPos]->IsLoaded())
            m_waitPatchChunkMap[neighborPos].insert(patchData);            // 즉시 큐잉
    }
}
```

원본 청크에 패치를 등록할 때마다 이 함수를 호출한다.

- **이웃 청크의 `m_cameraPatchChunkMap`에도 등록** — 이웃이 이후 언로드-재로드되어도 패딩 상태가 복원되도록.
- **이웃이 로드 상태이면 즉시 `m_waitPatchChunkMap`에 등록** — 이번 프레임의 PatchChunks가 처리하도록.

이 이중 등록이 **"패치는 시야 안팎을 넘나들어도 소실되지 않는다"**를 보장하는 근거다.

### 6.3 나무 배치에서의 패딩 전파

플레이어 패치뿐 아니라 나무 배치([ChunkStructure §3.2](../ChunkStructure/README.md))에서 발생한 크로스 청크 데이터도 같은 원리로 이웃 청크의 패딩에 반영된다. 이 경로는 `Chunk::Initialize()` 내부에서 `ChunkLoadMemory::loadPatchResult`에 축적된 뒤, `SyncLoadedChunks`가 `UpdatePatchChunkMap`으로 넘겨 최종적으로 `m_waitPatchChunkMap`에 진입한다.

## 7. 1 Frame Flickering의 근원과 `GetIndicesSize` 스냅샷

### 7.1 문제 상황

Chunk가 CPU 메시 데이터를 vector로 갖고 있다는 점이 원인이다. 이런 코드를 생각해보자.

```cpp
// 렌더 스레드 (메인 스레드)
Graphics::context->DrawIndexed((UINT)chunk->GetOpaqueIndices().size(), 0, 0);
```

`GetOpaqueIndices().size()`가 반환하는 값은 **호출 시점의 vector 크기**다. 문제는 이 시점에 **워커 스레드가 Patch 도중이라 vector를 재구성하는 중**일 수 있다는 것이다.

- Patch 워커는 `InitWorldVerticesData` 안에서 `ClearCpuVertices()` → `GreedyMeshing()`을 순차 실행한다.
- `ClearCpuVertices` 직후에는 vector가 비어 있고, GreedyMeshing이 다시 채운다.
- 렌더 스레드가 하필 그 사이에 `size()`를 읽으면 0 또는 중간 상태의 값을 받는다.

결과: 청크가 한 프레임 사라졌다가 다음 프레임에 돌아온다. **1프레임 flickering.**

또한 vector 자체가 재할당되는 순간 렌더 스레드가 GPU 버퍼를 바인딩한 상태에서 CPU 포인터가 무의미해질 수도 있다 (GPU 리소스는 매니저가 별도 소유하므로 이 부분은 안전하지만, `size()` 읽기 자체가 이미 인종의 데이터 레이스다).

### 7.2 해결 — count 스냅샷

Chunk는 vector와 별개로 **count 필드**를 갖는다.

```cpp
uint32_t m_opaqueVertexCount;
uint32_t m_opaqueIndexCount;
// (4종 각각)

void Chunk::UpdateCpuBufferCount()
{
    m_opaqueVertexCount = (uint32_t)m_opaqueVertices.size();
    m_opaqueIndexCount  = (uint32_t)m_opaqueIndices.size();
    // ...
}
```

렌더 시점에는 vector의 `size()`가 아니라 이 count를 조회한다.

```cpp
Graphics::context->DrawIndexed(chunk->GetOpaqueIndexCount(), 0, 0);   // ✓
// (X) DrawIndexed(chunk->GetOpaqueIndices().size(), 0, 0);
```

count 갱신 시점은 **Sync 함수 안에서 명시적으로 호출**한다 — `SyncLoadedChunks` 그리고 `SyncPatchedChunks`. 이 두 지점은 워커가 완료된 직후이며 아직 GPU 업로드 전이다. 즉:

```
워커: vector 채움
    ↓
Sync (메인 스레드): UpdateCpuBufferCount() → count 스냅샷
    ↓
Sync (메인 스레드): UpdateChunkGPUBuffer → GPU 업로드
    ↓
렌더 (메인 스레드): DrawIndexed(count) 사용
```

렌더 스레드가 참조하는 count는 항상 "**직전에 완료된 워커 작업 시점의 크기**"이며, 다음 워커가 vector를 만지고 있는 중이라도 이 값은 안정적이다.

GPU 버퍼도 마찬가지로 count와 동시에 스냅샷되므로 count와 GPU 버퍼 내용이 어긋나지 않는다.

**GPU 버퍼가 ChunkManager 소유라서 얻는 부수 효과**: Chunk의 CPU vector가 다음 워커에 의해 재구성되어도 GPU 버퍼는 그대로 남아 있다. 즉 CPU 데이터와 GPU 데이터의 라이프사이클이 분리되어 있어, count 스냅샷만으로도 렌더가 완전히 안전해진다.

## 8. DDA Picking 으로 패치 트리거하기

DDA(Digital Differential Analyzer)는 Voxen에서 **카메라 앞의 어느 블록을 조준했는가**를 판정하는 알고리즘이다. `Camera::DDAPickingBlock()`이 매 프레임 `Camera::Update()` 안에서 실행된다.

### 8.1 알고리즘 요약

DDA는 레이 원점에서 출발해 셀 경계를 따라 한 축씩 진행한다. "다음 x 경계까지의 t, 다음 y 경계까지의 t, 다음 z 경계까지의 t" 중 가장 작은 값의 축으로만 이동하는 것이 핵심이다.

```cpp
int step[3]  = { m_forward.x > 0 ? 1 : -1, ... };   // 각 축 진행 방향
float delta[3] = { fabs(1.f / m_forward.x), ... }; // 축 방향 단위 이동당 t 증가량

// 시작점에서 첫 셀 경계까지의 t
float side[3] = {
    (step[0] > 0) ? (floor(eye.x+1) - eye.x) * delta[0]
                  : (eye.x - floor(eye.x)) * delta[0],
    ...
};
```

매 반복:

1. `side[]` 중 최솟값을 가진 축을 선택.
2. 해당 축의 좌표(`cur*`)를 `step`만큼 이동, `side`에 `delta`를 더한다.
3. 그 축의 진입면(`face*`)이 이번 히트의 face 후보.
4. `HasObjectAt(cur)`이 true면 히트 확정.

### 8.2 히트 판정 — HasObjectAt

```cpp
bool ChunkManager::HasObjectAt(Vector3 pickingBlockPos)
{
    if (GetInstanceByPosition(pickingBlockPos))
        return true;
    const Block* b = GetBlockByPosition(pickingBlockPos);
    if (b && !Block::IsTransparency(b->GetType()))
        return true;
    return false;
}
```

- **인스턴스**(풀, 꽃, 덩굴)는 무조건 히트.
- **블록**은 투명 계열(Air, Water)이면 히트 미판정. 물속을 조준했다고 물을 뚫진 않는다.

### 8.3 최대 거리와 종료 조건

```cpp
while (min(min(sideX, sideY), sideZ) < 4.0f) { ... }
```

레이 진행 거리(t) 4.0을 상한으로 둔다. 카메라로부터 4블록 이내에서만 조준을 허용하는 게임플레이 결정이다.

히트하면 아래 정보가 채워지고 반복을 종료한다.

```cpp
m_hasPickingObject      = true;
m_pickingObjectPosition = 히트한 블록의 정수 좌표
m_pickingObjectFace     = 진입한 면 (LEFT/RIGHT/TOP/BOTTOM/FRONT/BACK)
```

### 8.4 마우스 입력 → 패치 요청

Camera Update의 마지막 블록에서 조준 결과가 있으면 마우스 입력을 소비한다.

```cpp
if (m_hasPickingObject) {
    if (mouseLeftDown)  ChunkManager::GetInstance()->RemoveBlockPatchAt(m_pickingObjectPosition);
    if (mouseRightDown) ChunkManager::GetInstance()->AddBlockPatchAt(m_pickingObjectPosition, m_pickingObjectFace);
}
```

### 8.5 RemoveBlockPatchAt — 블록 제거

```cpp
Vector3 chunkOffsetPos = Utils::CalcOffsetPos(pos, CHUNK_SIZE);
Vector3 localPos       = pos - chunkOffsetPos;

BLOCK_TYPE replaced = (pos.y <= Terrain::WATER_HEIGHT_LEVEL) ? BLOCK_WATER : BLOCK_AIR;

PatchData p = MakePatchData(localPos, replaced, Instance(), CHUNK_SIZE, false);

m_cameraPatchChunkMap[chunkPos].insert(p);                          // (a) 영구 저장
if (m_chunkMap.count(chunkPos) && m_chunkMap[chunkPos]->IsLoaded())
    m_waitPatchChunkMap[chunkPos].insert(p);                        // (b) 즉시 큐잉

PropagatePatchByEdgeBlock(localPos, chunkOffsetPos, replaced);       // (c) 패딩 전파
```

세 지점의 조합이 이 시스템의 핵심이다.

- **(a)** 이 청크가 언로드되어도 이 변경은 영속. 재로드 시 자동 복원.
- **(b)** 지금 로드 상태면 이번 프레임 안에 반영.
- **(c)** 경계 블록이었다면 이웃 청크의 패딩도 일관되게 갱신 ([§6](#6-greedy-meshing-패딩과-경계-전파)).

**대체 블록의 결정** — 수면 이하는 `BLOCK_WATER`로 채운다. 그렇지 않으면 부순 자리에 구멍이 나 물이 흐르지 않고 공기 블록으로 남는다.

### 8.6 AddBlockPatchAt — 블록 추가

Remove와 거의 동일하나 두 가지 차이.

**face 오프셋 적용** — 조준된 블록 자리에 얹는 게 아니라 조준면 **바깥쪽**에 얹는다.

```cpp
Vector3 faceOffset = 0;
switch (face) {
    case LEFT:   faceOffset.x = -1; break;
    case RIGHT:  faceOffset.x = +1; break;
    case BOTTOM: faceOffset.y = -1; break;
    case TOP:    faceOffset.y = +1; break;
    case FRONT:  faceOffset.z = -1; break;
    case BACK:   faceOffset.z = +1; break;
}
Vector3 placePos = pickingPos + faceOffset;
```

face 오프셋이 청크 경계를 넘으면 `CalcOffsetPos`가 자동으로 이웃 청크 좌표로 이동시킨다. 즉 조준된 블록과 실제로 놓이는 블록이 서로 다른 청크에 있을 수 있고, 이 경우 이웃 청크의 `m_waitPatchChunkMap`에 등록된다.

**대체 블록** — 놓이는 블록은 항상 `BLOCK_GOLD`. 게임플레이 데모용 기본값.

## 9. 회고

- **부분 재메싱**이 궁극의 성능 개선점이다. 현재 워커 스레드가 밀리초 단위로 소화하고 있어 눈에 띄지 않지만, 대규모 편집(폭발, 대량 파괴)이 도입되면 병목이 될 것이다.
- **자료구조 5종**(§5)이 개별로 이해되면 명료하지만, 삽입/삭제 시 이들의 정합성을 개별 코드에서 지켜야 한다. Load/Unload/Patch/Propagate 각 경로에서 이 다섯을 어떻게 만지는지 규칙을 코드로 표현했다면 실수 여지가 줄어든다. 그래프 자료구조로 캡슐화하는 게 자연스러운 다음 단계다.
- **DDA의 최대 거리 4.0 하드코딩**은 게임플레이가 확장되면 부적절해진다. 아이템/도구별 리치가 도입되면 파라미터화가 필요하다.
- **PatchDataHashSet의 동등성이 좌표만**으로 결정되는 것도 미묘한 함정이다. 같은 좌표에 두 번 패치가 들어오면 삽입 순서에 의해 결과가 갈린다. 우선순위 필드(예: 타임스탬프)를 도입하면 명확해진다.
- **face 오프셋으로 인한 크로스 청크 블록 추가**는 잘 동작하지만, "조준 청크"와 "패치 청크"가 다를 수 있다는 사실이 원거리 코드 리뷰 시 놓치기 쉽다. `MakePatchData`가 좌표 랩(wrap)을 처리할 수 있는 파라미터를 이미 갖고 있으니, 이를 활용해 조준 청크 기준으로 통일하는 리팩토링도 고려할 만하다.
