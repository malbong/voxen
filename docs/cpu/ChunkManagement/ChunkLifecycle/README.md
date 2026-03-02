# Chunk Lifecycle

## 1. 개요

ChunkUpdate가 매 프레임의 파이프라인 흐름을 다룬다면, ChunkLifecycle은 청크 하나가 **생성(Load) → 변경(Patch/Picking) → 소멸(Unload)** 과정에서 내부적으로 어떤 연산이 수행되는지를 다룬다.

```
                    ┌──────────────────────┐
        Pool에서    │                      │     Pool로
        할당        │       Loaded         │     반환
  ───────────────►  │                      │  ──────────────►
     Initialize()   │  Patch ◄── Picking   │     Clear()
                    │                      │
                    └──────────────────────┘
```

## 2. Load - 청크 초기화

### 2.1 진입 조건

카메라가 청크 경계를 넘으면 `UpdateChunkList()`가 새 범위를 순회한다. `m_chunkMap`에 존재하지 않는 위치를 발견하면 Object Pool에서 청크를 꺼내 `m_loadChunkList`에 넣는다.

`UpdateLoadChunkList()`에서 이 리스트를 카메라 XZ 거리순으로 정렬하고, 가까운 청크부터 `std::async`로 `Chunk::Initialize()`를 디스패치한다.

```cpp
// 정렬: 먼 것이 앞, 가까운 것이 뒤 → pop_back으로 가까운 것 우선 처리
std::sort(m_loadChunkList.begin(), m_loadChunkList.end(), /* XZ 거리 내림차순 */);

while (!m_loadChunkList.empty() && m_initFutures.size() < m_initThreadCount) {
    Chunk* chunk = m_loadChunkList.back();
    m_loadChunkList.pop_back();
    // ChunkLoadMemory를 풀에서 꺼내 함께 전달
    m_initFutures.push_back({chunk, std::async(&Chunk::Initialize, chunk, memory)});
}
```

### 2.2 Chunk::Initialize() 내부 파이프라인

Initialize는 워커 스레드에서 실행되며, 6단계를 순차 수행한다.

```
Chunk::Initialize(ChunkLoadMemory* memory)
│
├─ [1] InitTerrainNoises       노이즈 샘플링 (CHUNK_SIZE_P x CHUNK_SIZE_P)
├─ [2] InitBiomeMapAndCount    바이옴 결정 + 바이옴별 카운트
├─ [3] InitBasicBlockType      블록 타입 배정 (CHUNK_SIZE_P³ 순회)
├─ [4] InitTreePlace           나무 배치 + 크로스 청크 패치 데이터 생성
├─ [5] InitInstancePlace       인스턴스(풀, 꽃) 배치
└─ [6] InitWorldVerticesData   Binary Greedy Meshing → 버텍스/인덱스 생성
```

#### [1] InitTerrainNoises

패딩 포함 `CHUNK_SIZE_P(34) x CHUNK_SIZE_P(34)` 범위에서 7종의 노이즈를 샘플링한다. 패딩(±1)은 인접 청크와의 경계에서 블록 타입과 바이옴을 정확히 판단하기 위해 필요하다.

| 노이즈          | 용도                                |
| --------------- | ----------------------------------- |
| continentalness | 대륙/해양 구분                      |
| erosion         | 침식 정도                           |
| peaksValley     | 산/계곡 지형                        |
| temperature     | 기온 → 바이옴 결정                  |
| humidity        | 습도 → 바이옴 결정                  |
| distribution    | 오브젝트 분포 시드                  |
| elevation       | 최종 지형 높이 (위 5개 노이즈 조합) |

#### [2] InitBiomeMapAndCount

`CHUNK_SIZE(32) x CHUNK_SIZE(32)` 범위에서 노이즈 값을 기반으로 바이옴 타입을 결정한다. 바이옴별 타일 수(`biomeCount`)를 집계하여 이후 나무/인스턴스 배치의 비율 계산에 사용한다.

#### [3] InitBasicBlockType

패딩 포함 `CHUNK_SIZE_P³(34³)` 전체를 순회하며, 월드 좌표와 노이즈 값을 기반으로 각 위치의 블록 타입을 결정한다. 결과는 `m_blocks[x][y][z]`에 저장된다.

#### [4] InitTreePlace

시드 기반 랜덤으로 `TREE_PLACE_MAX_COUNT_PER_CHUNK(64)`개의 후보 위치를 생성한 뒤, 각 위치에서 배치 조건을 검사한다.

**배치 조건 (`CanPlaceTreeAt`)**:

- 바이옴별 최대 나무 수 비율 제한 (`GetMaxPlaceCountByBiomeRatio`)
- 청크 로컬 범위 내 (0~31)
- 바이옴에 맞는 나무 타입이 존재
- 주변 3x1x3 범위에서 바닥이 불투명 + 위가 AIR

**나무 생성 (`PlaceTree`)**:
`Tree::GenerateTreeShape()`로 `TREE_SIZE³` 크기의 3D 배열을 생성하고 순회하며:

```
TreeShape[dy][dz][dx]의 각 요소:
├─ TRUNK → SetTreeBlockType() : 줄기 블록 배치
├─ LEAF  → SetTreeBlockType() : 잎 블록 배치
└─ VINE  → SetTreeVines()     : 덩굴 인스턴스 배치
```

**크로스 청크 처리 (`SetTreeBlockType`)**:

나무는 최대 `TREE_SIZE(11)` 블록 크기로, 청크 경계를 넘을 수 있다. 이때 3가지 경우로 분기한다:

| 블록 위치                          | 처리                                                       |
| ---------------------------------- | ---------------------------------------------------------- |
| 청크 내부 + 패딩 범위 (-1~32)      | `m_blocks`에 직접 기록                                     |
| 청크 외부                          | `PatchData` 생성 → `chunkPatchDataMap`에 저장              |
| 내부/외부 경계 (0, 31 또는 -1, 32) | `GenerateEdgePatchEntry()`로 인접 청크 전파 패치 추가 생성 |

`chunkPatchDataMap`은 `ChunkLoadMemory`에 저장되며, Initialize 완료 후 메인 스레드에서 의존성 맵에 등록된다.

#### [5] InitInstancePlace

`INSTANCE_PLACE_MAX_COUNT_PER_CHUNK(256)`개의 후보 위치에 대해 두 종류의 인스턴스를 배치한다:

- **수면 인스턴스** (`SetWaterPlaneInstance`): 수면 높이(64) 청크에서만, elevation이 수면 아래인 위치에 수련잎 등을 배치
- **바이옴 인스턴스** (`SetBiomeInstance`): 바이옴별 비율 제한 적용, 온도/습도 기반 높이 결정, 청크 외부면 PatchData로 위임

각 인스턴스는 시드 기반으로 회전(`yawRotation`)과 XZ 오프셋 노이즈를 부여하여 자연스러운 배치를 만든다.

#### [6] InitWorldVerticesData

블록 배열을 Binary Greedy Meshing으로 버텍스/인덱스 데이터로 변환한다. 4종류의 메시를 독립적으로 생성한다:

| 메시 타입    | 대상 블록           | 용도               |
| ------------ | ------------------- | ------------------ |
| LowLod       | Opaque + SemiAlpha  | 원거리 LOD 렌더링  |
| Opaque       | 불투명 블록         | 메인 렌더링        |
| Transparency | 투명 블록 (물 등)   | 투명 패스 렌더링   |
| SemiAlpha    | 반투명 블록 (잎 등) | 반투명 패스 렌더링 |

### 2.3 Initialize 완료 후 처리 (메인 스레드)

future가 완료되면 메인 스레드에서 다음을 수행한다:

```
future.get() → ChunkLoadMemory* 반환
│
├─ [의존성 등록] chunkPatchDataMap 순회
│   ├─ m_patchDependencyMap[current][target]에 패치 데이터 저장
│   ├─ target이 이미 로드 + 아직 미적용 → m_patchChunkMap[target]에 추가 (패치 트리거)
│   ├─ m_lookupDependencySet[target]에 current 등록 (역방향 조회용)
│   └─ m_patchedChunkSet[target]에 current 기록 (중복 방지)
│
├─ [역방향 조회] m_lookupDependencySet에 current가 있는지 확인
│   └─ 이미 로드된 인접 청크가 나를 위해 만든 패치 데이터를 가져옴
│
├─ [플레이어 패치] m_cameraPatchChunkMap에 current가 있으면 병합
│
├─ UpdateCpuBufferCount() → UpdateChunkBuffer()  (GPU 업로드)
├─ SetUpdateRequired(true)  → 등장 애니메이션 시작
├─ SetLoad(true)            → 로드 완료 상태 전환
│
└─ ChunkLoadMemory::Clear() → 풀 반환
```

## 3. Unload - 청크 해제

### 3.1 진입 조건

`UpdateChunkList()`에서 새 범위에 포함되지 않는 로드된 청크를 `m_unloadChunkList`에 넣는다.

### 3.2 해제 과정

`UpdateUnloadChunkList()`에서 리스트를 순회하며 5개의 자료구조를 정리한다:

```
m_unloadChunkList 순회:
│
├─ [1] m_chunkMap.erase(chunkPos)
│       전역 청크 조회 맵에서 제거
│
├─ [2] m_patchChunkMap.erase(chunkPos)
│       대기 중인 패치 요청 제거
│
├─ [3] m_patchDependencyMap 정리
│   │   이 청크가 만든 모든 패치 의존성 제거
│   └─ 각 target의 m_lookupDependencySet에서 역참조도 제거
│       (빈 셋이 되면 엔트리 자체를 삭제)
│
├─ [4] m_patchedChunkSet.erase(chunkPos)
│       이 청크에 적용된 패치 이력 제거
│
├─ [5] chunk->Clear()
│   ├─ 플래그 초기화 (isLoaded, isPatching, isUpdateRequired)
│   ├─ instanceMap 클리어
│   ├─ CPU 버텍스/인덱스 벡터 클리어
│   └─ 버퍼 카운트 0으로 리셋
│
└─ ReleaseChunkToPool(chunk)
        Object Pool로 반환
```

역참조 정리가 중요한 이유: `m_lookupDependencySet`을 정리하지 않으면, 이후 해당 target 청크가 재로딩될 때 이미 존재하지 않는 source의 패치를 조회하려 시도한다.

## 4. Picking - 블록 추가/제거

### 4.1 트리거 조건

`ChunkManager::Update()`에서 카메라가 피킹 오브젝트를 가지고 있을 때 마우스 입력을 처리한다.

```cpp
if (camera.HasPickingObject()) {
    if (mouseLeftDown)  RemoveBlockPatchAt(camera.GetPickingObjectPosition());
    if (mouseRightDown) AddBlockPatchAt(camera.GetPickingObjectPosition(),
                                        camera.GetPickingObjectFace());
}
```

### 4.2 RemoveBlockPatchAt - 블록 제거

```
입력: 피킹된 블록의 월드 좌표
│
├─ 월드 좌표 → 청크 오프셋 + 로컬 좌표 분리
│
├─ 대체 블록 결정
│   ├─ y <= WATER_HEIGHT_LEVEL → BLOCK_WATER
│   └─ 그 외 → BLOCK_AIR
│
├─ PatchData 생성 (localX/Y/Z + Block)
│   ├─ m_cameraPatchChunkMap에 등록 (영구 보관)
│   └─ 청크가 로드 상태면 m_patchChunkMap에도 등록 (즉시 패치 트리거)
│
└─ PropagatePatchByEdgeBlock()
    └─ 로컬 좌표가 경계(0 또는 31)면 인접 청크에도 패치 전파
```

### 4.3 AddBlockPatchAt - 블록 추가

```
입력: 피킹된 블록의 월드 좌표 + 피킹된 face 방향
│
├─ face 방향으로 1블록 오프셋 적용
│   (LEFT: x-1, RIGHT: x+1, BOTTOM: y-1, TOP: y+1, FRONT: z-1, BACK: z+1)
│
├─ 오프셋된 좌표 → 청크 오프셋 + 로컬 좌표 분리
│   (face 오프셋이 청크 경계를 넘으면 다른 청크의 로컬 좌표가 됨)
│
├─ PatchData 생성 (블록 타입: BLOCK_GOLD)
│   ├─ m_cameraPatchChunkMap에 등록
│   └─ m_patchChunkMap에 등록
│
└─ PropagatePatchByEdgeBlock()
```

### 4.4 PropagatePatchByEdgeBlock - 경계 전파

블록이 청크 경계에 위치하면, 인접 청크의 Greedy Meshing에 영향을 준다. 이를 처리하기 위해 인접 청크에도 패치를 전파한다.

```
GenerateEdgePatchEntry(localX, localY, localZ, chunkPos, blockType)
│
├─ X축 경계 검사
│   x == 0  → 왼쪽 청크에 PatchData(localX=32, ...) 전파
│   x == 31 → 오른쪽 청크에 PatchData(localX=-1, ...) 전파
│
├─ Y축 경계 검사
│   y == 0  → 아래 청크에 PatchData(localY=32, ...) 전파
│   y == 31 → 위 청크에 PatchData(localY=-1, ...) 전파
│
└─ Z축 경계 검사
    z == 0  → 앞 청크에 PatchData(localZ=32, ...) 전파
    z == 31 → 뒤 청크에 PatchData(localZ=-1, ...) 전파
```

전파된 PatchData의 로컬 좌표가 -1 또는 32인 이유: 인접 청크의 패딩 영역(`m_blocks`의 인덱스 0 또는 33)에 해당 블록 정보를 기록하기 위해서다. 이 패딩 블록은 Face Culling 시 경계면의 가시성을 정확히 판단하는 데 사용된다.

### 4.5 cameraPatchChunkMap의 역할

`m_cameraPatchChunkMap`은 플레이어가 변경한 블록 정보를 **청크 생명주기와 무관하게 영구 보관**한다. 이것이 필요한 이유:

1. 플레이어가 블록을 제거/추가
2. 해당 청크가 카메라 범위 밖으로 나가 Unload
3. 다시 범위 안으로 들어와 Load
4. Initialize 완료 후, `m_cameraPatchChunkMap`에서 패치를 가져와 적용

Unload 시 `m_cameraPatchChunkMap`은 정리 대상에서 **제외**된다.

## 5. Patch - 청크 수정

### 5.1 패치 트리거 경로

패치가 `m_patchChunkMap`에 등록되는 3가지 경로:

| 경로            | 시점        | 원인                                 |
| --------------- | ----------- | ------------------------------------ |
| Picking         | Update 초반 | 플레이어 블록 추가/제거              |
| Initialize 완료 | Load 시     | 나무/인스턴스의 크로스 청크 전파     |
| 역방향 조회     | Load 시     | 인접 청크가 나를 위해 미리 만든 패치 |

### 5.2 UpdatePatchChunkMap 처리 흐름

```
m_patchChunkMap에서 키(청크 위치) 수집
│
├─ 카메라 거리순 정렬 (가까운 우선)
│
├─ 각 청크에 대해 유효성 검사:
│   ├─ m_chunkMap에 존재하는가?       → 없으면 제거
│   ├─ chunk 포인터가 유효한가?        → 없으면 제거
│   ├─ chunk->IsLoaded()인가?         → 아니면 제거
│   ├─ chunk->IsPatching()인가?       → 맞으면 건너뜀 (진행 중)
│   └─ m_patchFutures에 여유가 있는가? → 없으면 건너뜀
│
├─ ChunkLoadMemory 풀에서 할당
├─ std::async(Chunk::Patch, chunk, patchDataSet, memory) 디스패치
└─ m_patchChunkMap에서 해당 키 제거
```

### 5.3 Chunk::Patch() 내부 동작

```cpp
ChunkLoadMemory* Chunk::Patch(const PatchDataHashSet& patchDataSet, ChunkLoadMemory* memory)
```

워커 스레드에서 실행되며, PatchDataSet을 순회한다:

```
PatchDataSet 순회:
│
├─ Instance 패치 (patchData.instance != INSTANCE_NONE)
│   └─ 해당 위치의 블록이 투명(Transparency)이면:
│       ├─ 기존 인스턴스 제거 (있으면)
│       └─ 새 인스턴스 삽입
│
└─ Block 패치 (patchData.block != BLOCK_NONE)
    └─ 현재 블록과 타입이 다르면:
        ├─ m_blocks[x+1][y+1][z+1] = 새 블록
        ├─ m_onPatchDirtyFlag = true
        └─ 해당 위치의 인스턴스 제거 (있으면)
```

**PatchData의 동등성**:

`PatchDataEqual`은 `localX/Y/Z`만 비교한다. 같은 좌표에 두 번 패치가 들어오면 `unordered_set`에서 먼저 삽입된 것이 유지된다. 이는 나무 배치에서 같은 위치에 여러 청크가 패치를 보내는 경우 첫 번째 값을 우선하는 설계이다.

**Dirty Flag와 메시 재생성**:

`m_onPatchDirtyFlag`는 블록이 실제로 변경된 경우에만 true가 된다. 인스턴스만 변경된 경우 메시를 재생성하지 않는다(인스턴스는 별도 렌더링 경로 사용).

```
onPatchDirtyFlag == true:
└─ InitWorldVerticesData(memory)
    └─ Binary Greedy Meshing 전체 재실행
        (4종 메시: LowLod, Opaque, Transparency, SemiAlpha)
```

### 5.4 패치 완료 후 처리 (메인 스레드)

```
future.get() → ChunkLoadMemory* 반환
│
├─ UpdateCpuBufferCount()
│   (멀티스레드 환경에서 안전하게 버퍼 크기 스냅샷)
│
├─ onPatchDirtyFlag == true 일 때만:
│   └─ UpdateChunkBuffer(chunk)  → GPU 버퍼 재업로드
│
├─ SetIsPatching(false)
│
└─ ChunkLoadMemory::Clear() → 풀 반환
```

## 6. 청크 상태 전이 다이어그램

```
                         GetChunkFromPool()
  [Pool] ──────────────────────────────────► [Allocated]
    ▲                                            │
    │                                    SetOffsetPosition()
    │                                    m_loadChunkList에 추가
    │                                            │
    │                                            ▼
    │                                    [Loading / Queued]
    │                                            │
    │                                  std::async(Initialize)
    │                                            │
    │                                            ▼
    │                                    [Initializing]
    │                                    (워커 스레드)
    │                                            │
    │                               SetLoad(true)
    │                               SetUpdateRequired(true)
    │                                            │
    │                                            ▼
    │         ┌──────────────── [Loaded / Active] ◄──────────────┐
    │         │                   │          │                   │
    │         │            Picking 입력   Initialize 완료        │
    │         │            (플레이어)     (인접 청크)             │
    │         │                   │          │                   │
    │         │                   ▼          ▼                   │
    │         │              [Patch Queued]                      │
    │         │                      │                           │
    │         │            std::async(Patch)                     │
    │         │                      │                           │
    │         │                      ▼                           │
    │         │              [Patching]                          │
    │         │              (워커 스레드)                        │
    │         │                      │                           │
    │         │            SetIsPatching(false)                  │
    │         │                      │                           │
    │         │                      └───────────────────────────┘
    │         │
    │         │ UpdateChunkList()에서 범위 밖 판정
    │         ▼
    │    [Unloading]
    │         │
    │    chunkMap/dependency 정리
    │    chunk->Clear()
    │         │
    │         ▼
    └──── [Pool] ◄── ReleaseChunkToPool()
```

## 7. 회고

- Load 시 의존성 처리가 정방향/역방향 두 경로로 나뉘는데, 이는 청크 로딩 순서를 보장할 수 없는 비동기 환경에서 불가피한 설계다. A가 먼저 로드되든 B가 먼저 로드되든, 양쪽 모두 올바르게 패치가 적용되어야 하기 때문이다.
- Patch 시 메시 전체를 재생성하는 점은 구현이 단순하지만, 블록 하나 변경에도 32³ 범위의 Greedy Meshing을 다시 수행해야 한다. 변경 영역 주변만 부분 재생성하는 방식이 개선 방향이 될 수 있다.
- `PatchDataHashSet`이 좌표만으로 동등성을 판단하므로, 같은 위치에 여러 패치가 경합하면 삽입 순서에 의존한다. 타임스탬프 기반 우선순위를 두는 것도 고려할 수 있다.
