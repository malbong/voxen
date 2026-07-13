# Chunk Patch

<img width="600" height="420" alt="Image" src="https://github.com/user-attachments/assets/437ecc21-ae0e-433d-b403-f1da4df623d8" />

## 1. 개요

Patch는 이미 로드된 청크의 블록·인스턴스 상태를 **부분 변경**하는 시스템이다.

플레이어가 청크 내부를 수정하거나, 블록·인스턴스의 크기가 초기화 시 청크 경계를 넘는 경우 Patch를 통해 수정하도록 하는 방식이다.

원래는 존재하지 않았으나, World 생성 중 Tree 생성은 필수적이였고, 그에 따라 주변 청크에 대한 영향이 존재했고 Patch까지 도입하게 되었다.

## 2. Patch가 필요한 이유

Voxen에는 청크 하나의 초기화만으로 세계를 완성할 수 없는 세 가지 상황이 있다.

**(a) 크로스 청크 오브젝트 — 나무·인스턴스가 청크 경계를 넘음**  
나무 하나는 최대 `TREE_SIZE = 11` 블록의 3D 볼륨을 차지하며, 청크 경계에 존재하기 쉽다.
이때 나무의 일부 블록·덩굴 인스턴스는 이웃 청크에 기록되어야 하지만, 나무를 생성한 청크는 이웃 청크의 `m_blocks`를 직접 만질 수 없다 (스레드 안전성, 로드 순서 무관성).  
→ 데이터를 **패치 큐**에 넘겨두고 이웃 청크가 패치 정보를 받아 알아서 처리하도록 위임한다.

**(b) 플레이어의 세계 조작 — 블록 파괴/추가**  
마우스로 블록을 부수거나 추가할 때, 이미 로드된 청크의 `m_blocks`를 즉시 바꾸고 GPU 버퍼에 반영해야 한다.

**(c) Greedy Meshing이 요구하는 패딩 갱신**  
Chunk의 `m_blocks[34][34][34]`는 좌우 1칸씩 패딩을 갖는다.
이 패딩에는 인접 청크의 경계 블록 정보가 들어 있어야 Face Culling이 균일하게 동작한다. 나무가 청크 A의 경계에 자라면, 그 블록 정보는 **이웃 청크 B의 패딩**에도 반영되어야 한다.
→ Patch가 이 패딩 갱신까지 담당한다 ([§6](#6-greedy-meshing-패딩과-경계-전파) 참조).

## 3. Chunk::Patch — 워커 스레드에서의 실행

```cpp
ChunkLoadMemory* Chunk::Patch(const PatchDataHashSet& patchDataSet, ChunkLoadMemory* memory)
{
    m_isPatching = true;
    m_onPatchDirtyFlag = false;

    for (const PatchData& p : patchDataSet) {
        int x = p.localX, y = p.localY, z = p.localZ;

        // (a) Instance 패치
        // Instance가 존재할 수 있는 투명블록의 위치라면 패치 실행
        if (p.instance.GetType() != INSTANCE_NONE && Block::IsTransparency(m_blocks[x+1][y+1][z+1].GetType())) {

            if (IsInstanceAt(x, y, z)) m_instanceMap.erase({x,y,z});

            m_instanceMap.insert({{x,y,z}, Instance(p.instance)});
        }

        // (b) Block 패치
        if (p.block.GetType() != BLOCK_NONE) {

            // 타입이 바뀌었을 때만 패치를 했다고 판단
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
블록 한 칸이 바뀌면 이 4단계에서 다음이 모두 영향을 받는다.

```
1. 축별 column bit 생성       (블록 타입 분류)
2. Face Cull column bit       (인접 블록과의 관계로 컬링)
3. Face Slice column bit      (같은 타입/방향 슬라이스별로 재구성)
4. Greedy Meshing             (슬라이스 위 사각형 병합)
```

- 축별 column bit 가 바뀜에 따라 Face Cull의 결과도 바뀌고 순차적으로 영향을 받게 된다.
- 단순히 Greedy Meshing를 진행하지 않고, 단순히 1x1x1 메쉬로 구성하기에도 렌더링 품질에 영향을 미친다.
- 일부분만 다시 계산하기엔 사각형이 겹치거나 누락되는 등 여러 케이스가 존재할 것이고, 이를 종합적으로 정리하는 코드는 작성하기 힘들 것이다.
- `부분 재생성 구현 비용 > 부분 재생성 성능` 으로 판단하고 트레이드오프했다.
- 실제로 워커 쓰레드에서 재생성을 하기에 프레임에 영향을 미치지 않았다.

## 4. ChunkManager의 Patch 디스패치 + 동기화

Load와 마찬가지로 Patch도 **디스패치(`PatchChunks`)**와 **동기화(`SyncPatchedChunks`)**의 두 함수로 나뉜다.

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

        if (m_chunkMap.find(chunkPos) == m_chunkMap.end()) continue;     // (a)

        Chunk* chunk = m_chunkMap[chunkPos];
        if (!chunk->IsLoaded())   continue;                              // (b)

        if (chunk->IsPatching())  continue;                              // (c)

        ChunkLoadMemory* mem = GetChunkLoadMemoryFromPool();
        if (!mem) break;                                                // (d)

        m_patchFutures.push_back({chunk,
            std::async(std::launch::async, &Chunk::Patch, chunk,
                       std::move(m_waitPatchChunkMap[chunkPos]), mem)}); // 데이터 이동 시맨틱
        m_waitPatchChunkMap.erase(chunkPos);
    }
}
```

디스패치 전에 청크의 상태를 4가지 항목으로 검사한다.

- **(a) `m_chunkMap` 존재 여부 판단** — `m_waitPatchChunkMap`에서 따로 지우지 않는데, unload에서 이미 지워진 데이터라 처리할 필요가 없음.
- **(b) `IsLoaded()`** — 방어적 코드, 사실 `m_chunkMap[]`에 존재하는 모든 청크는 로드가 완료된 청크만 들어올 수 있음
- **(c) `IsPatching()`** — 이 청크는 이미 워커 스레드에서 Patch 중. 패치 중인 청크는 다음으로 유예됨.
- **(d) `ChunkLoadMemory` 풀 고갈** — 대여 실패 시 이번 프레임은 여기서 중단, 다음 프레임에 재시도.

### 4.2 SyncPatchedChunks — 동기화 + GPU 업로드

```cpp
for (auto it = m_patchFutures.begin(); it != m_patchFutures.end(); ) {
    if (it->second.wait_for(0us) != std::future_status::ready) { ++it; continue; }

    Chunk* chunk = it->first;
    ChunkLoadMemory* mem = it->second.get();
    it = m_patchFutures.erase(it);
    ReleaseChunkLoadMemoryToPool(mem);

    // 패치를 완료했는데, 이미 언로드된 위치인 경우는 무효
    const PosInt3 pos = Utils::VectorToPosInt3(chunk->GetOffsetPosition());
    if (m_chunkMap.find(pos) == m_chunkMap.end()) continue;

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
- **`UpdateCpuBufferCount()` 호출** — 이유는 [§7](#7-1-frame-flickering의-근원-과-getindicessize-스냅샷)에서 설명.

## 5. Patch 관련 자료구조

Patch 시스템은 여러 컨테이너로 상호작용한다.

- 소스→대상 방향의 이력 저장 (`m_patchDependencyMap`)
- 대상 관점에서의 빠른 역조회 (`m_lookupDependencySet`)
- 이미 적용된 소스 추적 (`m_patchedChunkSet`)
- 지금 워커에게 넘길 큐 (`m_waitPatchChunkMap`)
- 청크 라이프사이클과 분리된 영속 저장 (`m_cameraPatchChunkMap`)

```cpp
m_patchDependencyMap: 내가 영향을 미치는 청크와 패치정보를 담는 의존성 데이터
- m_patchDependencyMap[주체] = { { 객체1: {패치1}, {패치2}, ..}, { 객체2: {패치1}, ..}, ..}
- "나는 누구를 어떻게 패치해야 하는가?"

m_lookupDependencySet: 의존성을 찾는 LUT
- m_lookupDependencySet[객체] = { 주체1, 주체2, 주체3, .. }
- "나를 패치하라고 하는 다른 청크들이 있는가?"
- 패치를 하지 않아도 데이터 담아 차후에 사용: 아래의 m_patchedChunkSet과 성격이 조금 다름
 -> 다른 객체가 로드되든 말든 가지고 있는 데이터임

m_patchedChunkSet: patch 리스트에 등록된 정보를 담음, re Patch 방지
- m_patchedChunkSet[객체] = { 주체1, 주체2, ..}
- "나는 누구에 의해 패치를 했는가?"
- 패치 리스트에 넣은 경우에만 데이터 담아 사용함: 위의 m_lookupDependencySet와 성격이 조금 다름
 -> 객체가 로드되어 패치 리스트에 들어간 경우임

m_waitPatchChunkMap: patch할 청크들을 예약 목록에 담음
- m_waitPatchChunkMap[주체] = { 패치1, 패치2, .. }

m_cameraPatchChunkMap: 플레이어가 직접 청크를 수정 -> 영구적
- m_cameraPatchChunkMap[객체] = { 패치1, 패치2, ..}
```

### 5.6 왜 이렇게 많은가

Voxen은 **로드 순서에 의존하지 않는 세계 상태 일관성**을 목표로 해야했고, 다양한 경우의 수가 많았다.

- A가 B에게 남긴 패치는 B가 로드&언로드가 반복되어도 패치가 되어야 한다.
- A가 언로드 되어도 B는 패치 내용을 그대로 가지고 있어야 한다.
- A가 로드언로드 반복되어도 B에는 A에게 패치를 한번만 적용되면 된다.

Q. 단순히 `Patch[객체] = { 주체1: {패치...}, 주체2: {패치}}` 로 저장만 하면 되지 않을까?

- 결국 메모리를 언젠가 언로드시에 정리를 했어야 했고, `주체N`을 정리해야하기 때문에 모든 Patch List를 순회하긴 벅찰 것 같았다.
- 그래서 LUT(`m_lookupDependencySet`)를 구성하여 역참조를 구성한 것이다.
- cf. 현재 프로젝트는 `Patch[패치소스청크] = { 패치대상1: {}}`으로 반대 방향이다.

## 6. Greedy Meshing 패딩과 경계 전파

블록의 위치가 A 청크 내부이지만 맨 끝자리(`0` or `31`)는 A 청크를 이웃한 B 청크에서 패딩에 대한 정보로 갱신해야 Face Culling이 올바르게 동작한다. 그래서 실제로 블록이 주변 청크에 넘어가지 않더라도 Edge에 대해서 Patch를 실행했어야 했다.

### 6.1 좌표 변환 및 축 방향 모두 검사

로컬 좌표가 `0`인 블록은 **왼쪽 이웃 청크의 좌표 `32`(패딩)**에 해당한다. 마찬가지로 로컬 좌표 `31`인 블록은 **오른쪽 이웃 청크의 좌표 `-1` (=패딩 슬롯 0)**이다.

- 실제로 패치 시에 패딩에 대한 `1` offset을 더하기 때문에 `-1` or `32`을 사용한다. (-> `0` or `33` 패딩 좌표)

x방향 뿐만 아니라, y, z도 동일 로직을 적용한다.

- [0,0,0]의 블록인 경우 x, y, z 방향 모두에 패치를 전파해야 한다.

```cpp
// PatchData.h — static 멤버로 정의
static void PatchData::GenerateEdgePatchEntry(int x, int y, int z, Vector3 chunkPosition,
    BLOCK_TYPE blockType, int baseSize = 32, std::pair<PosInt3, PatchData>* outEntry, int& outCount)
{
    int xEdgeDir = (x == 0) ? -1 : ((x == baseSize - 1) ? 1 : 0);
    if (xEdgeDir) {
        Vector3 patchChunkOffsetPos = chunkPosition;
        patchChunkOffsetPos.x += xEdgeDir * baseSize;

        PosInt3 patchChunkOffsetPosInt3 = Utils::VectorToPosInt3(patchChunkOffsetPos);

        int newX = xEdgeDir == -1 ? baseSize : -1;

        PatchData patchData(newX, y, z, Block(blockType), Instance(), baseSize, false);

        outEdgePatchEntry[outEdgePatchEntryCount++] =
            std::make_pair(patchChunkOffsetPosInt3, patchData);
    }
    // y, z도 동일 로직
}
```

### 6.2 PropagatePatchByEdgeBlock — 전파 실행

해당 함수는 플레이어가 직접 블록을 제거하거나 추가할 때 실행되는 함수이다.

GenerateEdgePatchEntry가 최대 3개의 방향에 대한 패치 정보를 받아올 수 있으므로 그에 대한 처리를 반복으로 진행한다.

```cpp
void ChunkManager::PropagatePatchByEdgeBlock(
    Vector3 localPosition, Vector3 chunkOffsetPos, BLOCK_TYPE blockType)
{
    std::pair<PosInt3, PatchData> outEdgePatchEntry[3];
    int outCount = 0;
    PatchData::GenerateEdgePatchEntry(localPosition, chunkOffsetPos, blockType,
        Chunk::CHUNK_SIZE, outEdgePatchEntry, outCount);

    for (int i = 0; i < outCount; ++i) {
        auto& [neighborPos, patchData] = outEdgePatchEntry[i];

        m_cameraPatchChunkMap[neighborPos].insert(patchData);              // 영구 저장
        if (m_chunkMap.count(neighborPos) && m_chunkMap[neighborPos]->IsLoaded())
            m_waitPatchChunkMap[neighborPos].insert(patchData);            // 즉시 큐
    }
}
```

참고로, Chunk 내부에서 패치 전파는 해당 함수를 사용하지 않고 직접 전파한다.

```cpp
// Chunk.cpp -> SetTreeBlockType() 일부
// t-xyz Edge에 위치한 경우 local-XYZ 재정의 후 생성 및 전파
if (IsInnerEdge(tx, ty, tz) || IsOuterEdge(tx, ty, tz)) {
	Vector3 blockPos = m_offsetPosition + Vector3((float)tx, (float)ty, (float)tz);
	Vector3 blockOwnerOffsetPos = Utils::CalcOffsetPos(blockPos, CHUNK_SIZE);
	PosInt3 blockOwnerOffsetPosInt3 = Utils::VectorToPosInt3(blockOwnerOffsetPos);

	int localX = Utils::WrapToBase(tx, CHUNK_SIZE);
	int localY = Utils::WrapToBase(ty, CHUNK_SIZE);
	int localZ = Utils::WrapToBase(tz, CHUNK_SIZE);

	std::pair<PosInt3, PatchData> outEdgePatchEntry[3];
	int outEdgePatchEntryCount = 0;
	PatchData::GenerateEdgePatchEntry(localX, localY, localZ, blockOwnerOffsetPos, treeBlock,
		CHUNK_SIZE, outEdgePatchEntry, outEdgePatchEntryCount);

	PosInt3 myOffsetPosInt3 = Utils::VectorToPosInt3(m_offsetPosition);
	for (int i = 0; i < outEdgePatchEntryCount; ++i) {
		PosInt3& patchChunkPosInt3 = outEdgePatchEntry[i].first;
		PatchData& patchData = outEdgePatchEntry[i].second;

		if (patchChunkPosInt3 != myOffsetPosInt3) {
			memory->loadPatchResult[patchChunkPosInt3].insert(patchData);
		}
	}
}
```

## 7. 1 Frame Flickering의 문제와 `GetIndicesSize` 스냅샷으로 해결

### 7.1 문제 상황

프로젝트 성격상 Patch를 진행하고 있는 청크라고 해도, 렌더링의 연속성은 유지되어야 한다.
이 때, Patch를 워커가 진행하는 와중에도 렌더링이 되어야하고 데이터는 분리되어야했다.
메인 쓰레드에서 데이터를 읽을 때, 즉 렌더링을 할 때 cpu vertex vector 자체의 크기(`size()` 함수)를 기반으로 렌더링하는게 문제였다.

아래 코드를 보자.

```cpp
// 렌더 스레드 (메인 스레드)
Graphics::context->DrawIndexed((UINT)chunk->GetOpaqueIndices().size(), 0, 0);
```

`GetOpaqueIndices().size()`가 반환하는 값은 **호출 시점의 vector 크기**다. 문제는 이 시점에 **워커 스레드가 Patch 도중이라 vector를 재구성하는 중**일 수 있다는 것이다. (Load와는 관련은 없다. Load가 완료된 청크에 대해서만 렌더링 하기 때문이다.)

- Patch 워커는 `InitWorldVerticesData` 안에서 `ClearCpuVertices()` → `GreedyMeshing()`을 순차 실행한다.
- `ClearCpuVertices` 직후에는 vector가 비어 있고, GreedyMeshing이 다시 채운다.
- 렌더 스레드가 하필 그 사이에 `size()`를 읽으면 0 또는 중간 상태의 값을 받는다.

결과: 청크가 한 프레임 사라졌다가 다음 프레임에 돌아온다. **1프레임 flickering.**

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

렌더 시점에는 vector의 `size()`가 아니라 이 count를 조회하여 해결했다.

```cpp
Graphics::context->DrawIndexed(chunk->GetOpaqueIndexCount(), 0, 0);   // ✓
// (X) DrawIndexed(chunk->GetOpaqueIndices().size(), 0, 0);
```

count 갱신 시점은 **Sync 함수 안에서 명시적으로 호출**한다 — `SyncLoadedChunks` 그리고 `SyncPatchedChunks`.

- 이 두 지점은 워커가 완료된 직후이며 아직 GPU 업로드 전이다. 즉:

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

## 8. 회고

프로젝트를 진행하면서 거의 갖춰진 구조에서 Patch가 도입되었어야 했고, 그에 따라 많은 고민과 구현마저 어려웠던 마지막 CPU 구현 범위였던 챕터였다.

패치가 들어오면 전체 메쉬를 재생성하는 로직으로 통합하니 구현해야할 분리되어 편했지만, 성능 개선이 필요하다면 여기부터 봐야하지 않을까 싶다.

패치에 필요한 다양한 자료구조가 존재하고 개별로 보면 이해가 되지만, 더 간단한 방법이 있을텐데 떠오르지 않았다. 하나씩 unload할 때나 load할 때나 여러 코드에서 개별로 신경써줘야 했던 점이 까다로웠다.

Greedy Meshing의 Face Culling을 배제하고 구현을 완료하고 나서 보니, 청크 경계에서 메쉬가 겹치는 일이 많았다. 그래서 Edge에 대한 처리를 진행했고, 블록 하나의 변경으로 주변 청크 최대 3개에 패치를 전파하게 되었고, 그 패치마저 메쉬 재생성하는 비용이 들지만 결과는 만족했다.
