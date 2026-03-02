# Chunk Structure

## 1. 개요

Voxen의 월드는 Block, Chunk, ChunkManager 세 계층으로 구성된다. Block은 월드를 이루는 최소 단위이고, Chunk는 32x32x32개의 Block을 묶은 공간 단위이며, ChunkManager는 모든 Chunk를 Object Pool 기반으로 관리하는 싱글턴이다.

이 문서는 각 계층이 데이터를 어떻게 보유하고 참조하는지, 구조 중심으로 정리한다.

## 2. Block - 최소 단위

### 2.1 데이터 구조

```cpp
class Block {
    BLOCK_TYPE m_type;  // uint8_t (1바이트)
};
```

Block은 `BLOCK_TYPE` 열거형 하나만 저장한다. 현재 약 40종의 블록 타입이 정의되어 있으며, 크게 3가지 렌더링 분류로 나뉜다.

| 분류         | 예시                          | 특성                    |
| ------------ | ----------------------------- | ----------------------- |
| Opaque       | Grass, Stone, Dirt, Log 등    | 불투명, 뒷면 컬링 가능  |
| Transparency | Water, Air                    | 투명, 별도 패스 렌더링  |
| SemiAlpha    | Oak Leaf, Spruce Leaf 등      | 반투명, 알파 테스트 적용 |

### 2.2 블록 속성 테이블 (BlockTypeInfoSet)

각 블록 타입의 텍스처 인덱스와 렌더링 분류는 `BlockTypeInfoSet`에서 관리한다.

```cpp
class BlockTypeInfo {
    uint8_t m_texTopIndex;     // Top 면 텍스처
    uint8_t m_texSideIndex;    // Side 면 텍스처
    uint8_t m_texBottomIndex;  // Bottom 면 텍스처
    bool m_isTransparency;
    bool m_isOpaque;
    bool m_isSemiAlpha;
};
```

블록 타입에 따라 면마다 다른 텍스처를 가질 수 있다. 예를 들어 `BLOCK_GRASS`는 Top=잔디, Side=잔디+흙, Bottom=흙 텍스처를 사용한다.

## 3. Instance - 비블록 오브젝트

Block으로 표현하기 어려운 풀, 꽃, 덩굴 같은 오브젝트는 Instance로 처리한다. Block이 정육면체 메시라면, Instance는 Cross(X자), Fence(ㅁ자), Square(벽면), Floor(바닥면) 등의 경량 메시를 사용한다.

```cpp
class Instance {
    INSTANCE_TYPE m_type;              // 인스턴스 종류
    TEXTURE_INDEX m_texIndex;          // 텍스처 인덱스
    float m_yawRotation;               // Y축 회전 (0~360)
    Vector2 m_offsetNoisePositionXZ;   // XZ 위치 노이즈 오프셋
    uint8_t m_faceFlag;                // 덩굴용 면 플래그
};
```

Instance는 Block처럼 3D 배열에 저장되지 않고, 청크 내 `PosHashMap<Instance>`에 좌표 키로 저장된다. 인스턴스가 없는 위치에는 엔트리 자체가 존재하지 않아 메모리를 절약한다.

## 4. Chunk - 32x32x32 블록 공간

### 4.1 Block 저장 방식

```cpp
class Chunk {
    static const int CHUNK_SIZE   = 32;
    static const int CHUNK_SIZE_P = 34;  // CHUNK_SIZE + 2 (패딩 포함)

    Block m_blocks[CHUNK_SIZE_P][CHUNK_SIZE_P][CHUNK_SIZE_P];  // 34x34x34
    PosHashMap<Instance> m_instanceMap;
};
```

실제 블록 데이터는 `32x32x32 = 32,768개`이지만, 배열 크기는 `34x34x34 = 39,304개`이다. 상하좌우전후로 1칸씩 패딩이 추가되어 있다.

```
     인덱스:  0   1   2   3  ...  32  33
             ┌───┬───┬───┬───────┬───┬───┐
             │ P │   │   │  ...  │   │ P │
             └───┴───┴───┴───────┴───┴───┘
              패딩  ◄── 실제 데이터 (32칸) ──►  패딩
```

블록 `(x, y, z)`에 접근할 때는 항상 `m_blocks[x+1][y+1][z+1]`로 오프셋을 적용한다.

### 4.2 패딩이 필요한 이유

Binary Greedy Meshing에서 면 컬링(Face Culling)을 수행할 때, 각 블록의 6방향 이웃을 검사한다. 청크 경계에 위치한 블록은 인접 청크의 블록 정보가 필요한데, 패딩에 그 정보를 미리 저장해두면 경계 분기 없이 동일한 로직으로 처리할 수 있다.

```
  인접 청크 A          현재 청크           인접 청크 B
  ┌────────┐    ┌─────────────────┐    ┌────────┐
  │   ...  │31  │P│ 0  1 ... 31 │P│  0│  ...   │
  └────────┘    └─────────────────┘    └────────┘
                 ▲                  ▲
                 │                  │
          A의 경계 블록을      B의 경계 블록을
          패딩[0]에 복사       패딩[33]에 복사
```

패딩 데이터는 Chunk 초기화 시 직접 계산한다. `InitBasicBlockType()`에서 `CHUNK_SIZE_P(34)` 범위를 순회하므로, 패딩 영역의 블록 타입도 동일한 노이즈 기반으로 결정된다. 이후 인접 청크의 나무가 경계를 넘는 경우에는 Patch 시스템을 통해 패딩 블록이 갱신된다.

### 4.3 메시 데이터

Chunk는 블록 배열을 Binary Greedy Meshing으로 변환하여 4종류의 버텍스/인덱스 데이터를 생성한다.

```cpp
// 4종류의 메시 버퍼 (CPU 측)
std::vector<VoxelVertex> m_lowLodVertices;        // 원거리 LOD용
std::vector<VoxelVertex> m_opaqueVertices;        // 불투명 블록
std::vector<VoxelVertex> m_transparencyVertices;  // 투명 블록 (물)
std::vector<VoxelVertex> m_semiAlphaVertices;     // 반투명 블록 (잎)
// 각각에 대응하는 인덱스 벡터도 존재
```

`VoxelVertex`는 위치, 법선, 텍스처 인덱스, AO 등을 `uint32_t` 하나에 비트 패킹하여 저장한다.

```cpp
struct VoxelVertex {
    uint32_t data;  // 비트 패킹: 위치(15bit) + 법선(3bit) + 텍스처(8bit) + AO(2bit) + ...
};
```

### 4.4 청크 식별 및 좌표

```cpp
UINT m_id;                  // Object Pool 내 고유 ID (GPU 버퍼 인덱싱에 사용)
Vector3 m_offsetPosition;   // 청크의 월드 시작 좌표 (32의 배수)
Vector3 m_position;         // 실제 렌더링 위치 (등장 애니메이션에 사용)
```

`m_offsetPosition`은 청크의 논리적 위치이고, `m_position`은 렌더링 시 사용되는 위치다. 청크가 새로 로드될 때 `m_position.y`를 아래에서 시작하여 `m_offsetPosition.y`까지 올려보내는 등장 애니메이션을 적용한다.

### 4.5 Chunk 전체 구조 요약

```
Chunk
├── m_id                      Pool 내 고유 ID
├── m_offsetPosition          월드 시작 좌표
├── m_position                렌더링 위치
│
├── m_blocks[34][34][34]      블록 3D 배열 (패딩 포함)
│   └── Block
│       └── m_type (uint8_t)  블록 타입
│
├── m_instanceMap             인스턴스 해시맵
│   └── PosInt3 → Instance
│       ├── m_type            인스턴스 종류
│       ├── m_texIndex        텍스처 인덱스
│       ├── m_yawRotation     Y축 회전
│       ├── m_offsetNoiseXZ   위치 노이즈
│       └── m_faceFlag        덩굴 면 플래그
│
├── 메시 데이터 (CPU)
│   ├── m_lowLodVertices / Indices
│   ├── m_opaqueVertices / Indices
│   ├── m_transparencyVertices / Indices
│   └── m_semiAlphaVertices / Indices
│
├── 상태 플래그
│   ├── m_isLoaded            로드 완료 여부
│   ├── m_isPatching          패치 진행 중 여부
│   ├── m_isUpdateRequired    등장 애니메이션 진행 중
│   └── m_onPatchDirtyFlag    패치 후 메시 재생성 필요
│
└── m_constantData            GPU 상수 버퍼 데이터
    └── world (Matrix)        월드 변환 행렬
```

## 5. ChunkManager - 청크 전체 관리

### 5.1 핵심 상수

```cpp
static const int MAX_RENDER_DISTANCE = 320;       // Camera: 블록 단위 최대 렌더 거리
static const int CHUNK_SIZE = 32;                  // Chunk: 한 변의 블록 수

// ChunkManager에서 계산
static const int CHUNK_COUNT   = 2 * (320 / 32) + 1 = 21;   // XZ축 청크 수
static const int CHUNK_COUNT_P = 21 + 2 = 23;                // 패딩 포함
static const int MAX_HEIGHT_CHUNK_COUNT   = 8;                // Y축 청크 수 (256 / 32)
static const int MAX_HEIGHT_CHUNK_COUNT_P = 8 + 2 = 10;      // 패딩 포함
static const int CHUNK_POOL_SIZE = 23 * 23 * 10 = 5,290;     // Object Pool 크기
```

카메라를 중심으로 XZ 21x21, Y 8단의 격자에 청크를 배치한다. Pool 크기에 패딩을 포함하는 이유는, 카메라 이동 시 새 범위의 청크를 로드하면서 동시에 이전 범위의 청크를 해제하는 전환 기간에 양쪽 모두 메모리가 필요하기 때문이다.

### 5.2 Object Pool

```cpp
std::vector<Chunk*> m_chunkPool;  // 미사용 청크 스택
```

시작 시 `CHUNK_POOL_SIZE(5,290)`개의 Chunk를 미리 할당하여 풀에 넣어둔다. 새 청크가 필요하면 `GetChunkFromPool()`로 꺼내고, 더 이상 필요 없으면 `ReleaseChunkToPool()`로 반환한다. 런타임에 `new/delete`가 발생하지 않는다.

```
GetChunkFromPool()       ReleaseChunkToPool()
       │                         ▲
       ▼                         │
┌──────────────────────────────────────┐
│  m_chunkPool (vector, stack처럼 사용) │
│  [Chunk*] [Chunk*] [Chunk*] ...      │
└──────────────────────────────────────┘
```

### 5.3 청크 조회 맵 (chunkMap)

```cpp
PosHashMap<Chunk*> m_chunkMap;  // 위치 → 청크 포인터
```

`PosInt3(x, y, z)` 좌표를 키로 청크를 O(1)에 조회하는 해시맵이다. 월드의 모든 활성 청크(로딩 중 + 로드 완료)가 등록되어 있다.

- 카메라가 청크 경계를 넘으면 `UpdateChunkList()`에서 새 범위를 순회하고, `m_chunkMap`에 없는 위치는 Pool에서 꺼내 등록한다.
- 기존 범위에 있었지만 새 범위에 포함되지 않는 청크는 Unload 리스트로 이동 후 맵에서 제거된다.

### 5.4 GPU 버퍼 관리

ChunkManager는 모든 청크의 GPU 버퍼를 중앙에서 관리한다. 각 버퍼 배열은 `CHUNK_POOL_SIZE` 크기로, 청크의 `m_id`를 인덱스로 사용한다.

```cpp
// 청크 ID로 인덱싱되는 GPU 버퍼 배열
std::vector<ComPtr<ID3D11Buffer>> m_opaqueVertexBuffers;       // [CHUNK_POOL_SIZE]
std::vector<ComPtr<ID3D11Buffer>> m_opaqueIndexBuffers;        // [CHUNK_POOL_SIZE]
std::vector<ComPtr<ID3D11Buffer>> m_transparencyVertexBuffers; // [CHUNK_POOL_SIZE]
std::vector<ComPtr<ID3D11Buffer>> m_transparencyIndexBuffers;  // [CHUNK_POOL_SIZE]
std::vector<ComPtr<ID3D11Buffer>> m_semiAlphaVertexBuffers;    // [CHUNK_POOL_SIZE]
std::vector<ComPtr<ID3D11Buffer>> m_semiAlphaIndexBuffers;     // [CHUNK_POOL_SIZE]
std::vector<ComPtr<ID3D11Buffer>> m_lowLodVertexBuffers;       // [CHUNK_POOL_SIZE]
std::vector<ComPtr<ID3D11Buffer>> m_lowLodIndexBuffers;        // [CHUNK_POOL_SIZE]
std::vector<ComPtr<ID3D11Buffer>> m_constantBuffers;           // [CHUNK_POOL_SIZE]
```

청크가 Pool에서 나올 때 이미 `m_id`가 고정되어 있으므로, 같은 ID 슬롯의 버퍼를 재활용한다. 이전 데이터보다 큰 버퍼가 필요하면 `DXUtils::ResizeBuffer()`로 재생성하고, 크기가 충분하면 `DXUtils::UpdateBuffer()`로 덮어쓴다.

### 5.5 인스턴스 버퍼 관리

인스턴스는 블록과 다른 렌더링 경로를 사용한다. 4종류의 인스턴스 형상(Cross, Fence, Square, Floor)별로 공유 메시 버퍼와 인스턴스 정보 버퍼를 관리한다.

```cpp
// 형상별 공유 메시 (게임 내내 불변)
std::vector<ComPtr<ID3D11Buffer>> m_instanceVertexBuffers;  // [4] Cross/Fence/Square/Floor
std::vector<ComPtr<ID3D11Buffer>> m_instanceIndexBuffers;   // [4]

// 매 프레임 갱신되는 인스턴스 정보
std::vector<ComPtr<ID3D11Buffer>> m_instanceInfoBuffers;    // [4]
std::vector<std::vector<InstanceInfoVertex>> m_instanceInfoList;  // [4]
```

매 프레임 렌더링 대상 청크의 인스턴스를 수집하여 `m_instanceInfoList`를 구성하고, `DrawIndexedInstanced()`로 한 번에 렌더링한다.

### 5.6 청크 목록 관리

ChunkManager는 청크의 상태와 용도에 따라 여러 리스트를 운용한다.

```
m_chunkPool                전체 청크 Object Pool (미사용 청크)
m_chunkMap                 활성 청크 해시맵 (위치 → 청크)
│
├── m_loadChunkList        로딩 대기 청크 리스트
├── m_unloadChunkList      해제 대기 청크 리스트
│
├── m_renderChunkList      이번 프레임 렌더링 대상 (Frustum Culling 통과)
├── m_renderMirrorChunkList  반사 렌더링 대상
└── m_renderShadowChunkList  그림자 렌더링 대상
```

### 5.7 멀티스레드 구조

```cpp
uint32_t m_initThreadCount;   // 초기화 워커 수 (1~3)
uint32_t m_patchThreadCount;  // 패치 워커 수 (1~2)

std::vector<std::pair<Chunk*, std::future<ChunkLoadMemory*>>> m_initFutures;
std::vector<std::pair<Chunk*, std::future<ChunkLoadMemory*>>> m_patchFutures;

std::vector<ChunkLoadMemory*> m_chunkLoadMemoryPool;  // 스레드별 작업 메모리
```

워커 스레드는 `std::async`로 디스패치되며, `ChunkLoadMemory`를 풀에서 빌려 사용한다. `ChunkLoadMemory`에는 노이즈 배열, 바이옴 맵, 컬럼 비트 배열 등 초기화/패치 과정에서 필요한 임시 데이터가 담겨 있으며, 작업 완료 후 Clear하여 풀에 반환된다.

스레드 수는 하드웨어에 따라 자동 결정된다.

```cpp
uint32_t maxThreads = min(6, hardware_concurrency());
uint32_t usableThreads = max(maxThreads - 1, 1);  // 메인 스레드 1개 예약
m_initThreadCount  = clamp(usableThreads - 1, 1, 3);
m_patchThreadCount = clamp(usableThreads - initThreadCount, 1, 2);
```

### 5.8 ChunkManager 전체 구조 요약

```
ChunkManager (Singleton)
│
├── Object Pool
│   └── m_chunkPool [5,290개 Chunk*]
│
├── 활성 청크 관리
│   ├── m_chunkMap            PosInt3 → Chunk*
│   ├── m_loadChunkList       로딩 대기 목록
│   ├── m_unloadChunkList     해제 대기 목록
│   └── m_renderChunkList     렌더링 대상 목록 (+ Mirror, Shadow)
│
├── 패치 의존성 시스템
│   ├── m_patchDependencyMap  source → target → PatchDataSet
│   ├── m_lookupDependencySet target → {sources}  (역방향 조회)
│   ├── m_patchedChunkSet     target → {적용완료 sources}
│   ├── m_patchChunkMap       패치 대기 큐
│   └── m_cameraPatchChunkMap 플레이어 패치 영구 저장
│
├── GPU 버퍼 (청크 ID로 인덱싱)
│   ├── Vertex/Index Buffers × 4종 (LowLod, Opaque, Transparency, SemiAlpha)
│   ├── Constant Buffers     월드 변환 행렬
│   └── Instance Buffers     형상별 공유 메시 + 인스턴스 정보
│
└── 멀티스레드
    ├── m_initFutures         초기화 워커 (1~3)
    ├── m_patchFutures        패치 워커 (1~2)
    └── m_chunkLoadMemoryPool 워커용 작업 메모리 풀
```

## 6. 전체 계층 관계

```
ChunkManager
│
├── m_chunkPool ──────────── Chunk[0] ──┬── m_blocks[34][34][34] ── Block (m_type)
│                            Chunk[1]   └── m_instanceMap ── PosInt3 → Instance
│                            Chunk[2]
│                             ...
│                            Chunk[5289]
│
├── m_chunkMap ── PosInt3(x,y,z) → Chunk* ──── (위와 동일한 Chunk 참조)
│
└── GPU Buffers ── [chunkID] → Vertex/Index/Constant Buffer
```

Pool의 Chunk는 `m_id`로 고유 식별되며, `m_chunkMap`을 통해 월드 좌표로도 참조된다. GPU 버퍼는 `m_id`로 인덱싱되어, 같은 Chunk 객체가 다른 위치에 재활용될 때 동일한 버퍼 슬롯을 재사용한다.

## 7. 회고

- Block이 `BLOCK_TYPE(uint8_t)` 하나만 저장하는 구조는 메모리 효율적이지만, 블록에 추가 상태(조명 레벨, 손상도 등)를 부여하려면 구조 변경이 필요하다. 비트 패킹을 확장하거나 별도 데이터 레이어를 두는 방식을 고려해볼 수 있다.
- Instance를 HashMap으로 관리하는 것은 희소 데이터에 적합하지만, 인스턴스가 밀집된 바이옴(숲, 늪 등)에서는 해시 충돌로 인한 오버헤드가 발생할 수 있다. 청크별 인스턴스 수에 상한(`MAX_INSTANCE_BUFFER_SIZE`)을 두어 이를 제어하고 있다.
- Object Pool이 고정 크기인 점은 런타임 할당을 방지하지만, 렌더 거리를 동적으로 변경하기 어렵게 만든다. Pool 크기를 렌더 거리에 연동하여 재할당하는 구조로 개선할 수 있다.
