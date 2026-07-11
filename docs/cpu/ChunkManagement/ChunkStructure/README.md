# Chunk Structure

## 1. 개요

Chunk는 Voxen 월드를 이루는 32×32×32 블록 크기의 공간 단위이다. 자체적으로 지형/바이옴/블록 타입/나무/인스턴스를 결정하고, 이를 기반으로 렌더링에 사용할 CPU 메시 데이터(버텍스/인덱스)까지 스스로 생성한다.

Chunk는 자기 자신의 CPU 데이터만 소유하며, GPU 리소스(D3D11 Buffer)와 라이프사이클(로드/언로드/패치 스케줄링)은 상위 관리자인 [ChunkManager](../ChunkManager/README.md)가 담당한다. 이 문서는 Chunk가 어떤 데이터를 가지고 있고, 그 데이터가 초기화되는 순서, 그리고 왜 CPU 메시 데이터를 4종으로 분리했는지에 집중한다.

## 2. Chunk가 보유하는 데이터

### 2.1 블록 배열 - m_blocks

```cpp
Block m_blocks[CHUNK_SIZE_P][CHUNK_SIZE_P][CHUNK_SIZE_P];
// CHUNK_SIZE   = 32
// CHUNK_SIZE_P = 34   (32 + 좌우 패딩 1칸씩)
```

실제 데이터는 32³이지만 배열 크기는 34³이다. 상하좌우전후 각 1칸씩의 **패딩**이 존재하며, 인접 청크의 경계 블록 정보를 이 패딩에 미리 채워두면 Face Culling 로직이 청크 경계 분기 없이 균일하게 동작한다. Block 하나는 `BLOCK_TYPE(uint8_t)` 한 필드만 저장한다.

블록 접근 시에는 항상 `m_blocks[x+1][y+1][z+1]`로 오프셋을 적용한다.

### 2.2 인스턴스 맵 - m_instanceMap

```cpp
PosHashMap<Instance> m_instanceMap;   // PosInt3(x,y,z) -> Instance
```

풀, 꽃, 덩굴처럼 정육면체 메시로 표현하기 어려운 오브젝트는 별도 `Instance` 자료구조로 관리한다. Block처럼 3D 배열로 두면 대부분의 슬롯이 비어 낭비가 크므로, 좌표를 키로 하는 해시맵에 **희소 저장**한다.

Instance는 형상 타입(Cross/Fence/Square/Floor), 텍스처 인덱스, Y축 회전, 위치 오프셋 노이즈, 면 플래그(덩굴용)를 담는다.

### 2.3 CPU 메시 데이터 - 4종 분리

```cpp
std::vector<VoxelVertex> m_lowLodVertices;        // + 인덱스
std::vector<VoxelVertex> m_opaqueVertices;        // + 인덱스
std::vector<VoxelVertex> m_transparencyVertices;  // + 인덱스
std::vector<VoxelVertex> m_semiAlphaVertices;     // + 인덱스
```

블록 배열을 Binary Greedy Meshing으로 변환한 결과가 이곳에 저장된다. 4종으로 분리한 이유는 [§4](#4-cpu-메시-데이터를-4종으로-분리한-이유)에서 다룬다.

`VoxelVertex`는 위치, 법선, 텍스처 인덱스, AO 등을 `uint32_t` 하나에 비트 패킹하여 메모리를 절약한다.

### 2.4 상태 플래그와 좌표

```cpp
UINT    m_id;                  // Object Pool 슬롯 ID (GPU 버퍼 인덱스 겸용)
Vector3 m_offsetPosition;      // 논리적 월드 좌표 (32의 배수)
Vector3 m_position;            // 렌더링용 좌표 (등장 애니메이션 진행 위치)

bool    m_isLoaded;            // Initialize 완료 여부
bool    m_isPatching;          // 패치 진행 중 여부
bool    m_isUpdateRequired;    // 등장 애니메이션 진행 중
bool    m_onPatchDirtyFlag;    // 패치로 블록이 실제로 바뀌었는가 (메시 재생성 필요)

ChunkConstantData m_constantData;   // world 변환 행렬 (GPU 상수 버퍼로 업로드될 데이터)
```

`m_id`는 ChunkManager의 Object Pool에서 청크가 생성될 때 부여되며, 언로드/재활용 사이에도 **불변**이다. 이 ID가 그대로 ChunkManager의 GPU 버퍼 배열 인덱스로 쓰인다([§5](#5-gpu-버퍼는-왜-chunk가-아니라-chunkmanager가-소유하는가) 참조).

## 3. Initialize - 청크 데이터 초기화 순서

`Chunk::Initialize()`는 워커 스레드에서 호출되어 청크 하나의 모든 CPU 데이터를 완성한다. 6개 단계를 **순차적으로** 수행하며, 각 단계는 앞 단계의 결과를 입력으로 사용한다.

```
Chunk::Initialize(offsetPosition, memory)
│
├─ [1] InitTerrainNoises          -> noise 배열 (7종) 채움
├─ [2] InitBiomeMapAndCount       -> biomeMap2D + biomeCount 채움
├─ [3] InitBasicBlockType         -> m_blocks 전체 초기 배정
├─ [4] InitTreePlace              -> 나무 배치 + 크로스 청크 패치 데이터 생성
├─ [5] InitInstancePlace          -> m_instanceMap 채움 (+ 크로스 청크 패치)
├─ [6] InitWorldVerticesData      -> CPU 메시 4종 생성
│
└─ m_position / m_constantData.world 세팅 (등장 애니메이션 시작 위치)
```

`memory`는 `ChunkLoadMemory*`로, 각 단계가 공유하는 대용량 임시 버퍼(노이즈 배열, 컬럼 비트 배열 등)를 담는 컨테이너이다. 스레드마다 하나씩 풀에서 대여하는 구조이며, 라이프사이클 관리는 [ChunkManager §3.2](../ChunkManager/README.md)에서 다룬다.

### 3.1 각 단계 요약

| # | 단계                    | 하는 일                                                                       | 상세 문서                                                                                                                     |
| - | ----------------------- | ----------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------ |
| 1 | InitTerrainNoises       | 패딩 포함 34×34 격자에서 7종 노이즈 샘플링. 이후 단계 전체의 입력이 된다.     | [Terrain](../../WorldGeneration/Terrain/README.md)                                                                            |
| 2 | InitBiomeMapAndCount    | 32×32 격자의 바이옴 타입 결정, 바이옴별 타일 카운트 집계.                     | [Biome](../../WorldGeneration/Biome/README.md)                                                                                |
| 3 | InitBasicBlockType      | 패딩 포함 34³ 전 지점의 초기 블록 타입 결정 후 `m_blocks`에 기록.             | [BlockType](../../WorldGeneration/BlockType/README.md)                                                                        |
| 4 | InitTreePlace           | 시드 기반 랜덤으로 나무 배치. 청크 경계를 넘는 블록은 `PatchData`로 위임.     | [Tree](../../WorldGeneration/Tree/README.md)                                                                                  |
| 5 | InitInstancePlace       | 풀·꽃 배치. 청크 외부로 벗어나는 위치는 마찬가지로 `PatchData`로 위임.        | -                                                                                                                              |
| 6 | InitWorldVerticesData   | `m_blocks`를 Binary Greedy Meshing으로 4종 CPU 메시 데이터로 변환.            | [BinaryBlockInfo](../../MeshOptimization/BinaryBlockInfo/README.md), [BinaryGreedyMeshing](../../MeshOptimization/BinaryGreedyMeshing/README.md) |

각 단계의 알고리즘 세부 사항은 개별 문서에 위임하고, 여기서는 **"청크 초기화가 어떤 데이터를 어떤 순서로 채우는가"**만 다룬다.

### 3.2 크로스 청크 데이터의 위탁

InitTreePlace/InitInstancePlace 단계에서 청크 경계를 넘는 블록·인스턴스가 발생하면, 자기 자신의 `m_blocks`에 기록하지 못한다. 이 경우 해당 데이터를 `ChunkLoadMemory::loadPatchResult`에 (대상 청크 위치, `PatchData`) 형태로 저장하고, Initialize 반환 후 ChunkManager가 수집하여 대상 청크의 패치 큐로 전달한다.

이 위탁 흐름은 [Patch 문서](../ChunkPatch/README.md)에서 다룬다.

## 4. CPU 메시 데이터를 4종으로 분리한 이유

Chunk는 하나의 블록 배열에서 4개의 독립된 메시(LowLod / Opaque / Transparency / SemiAlpha)를 생성한다. 이는 렌더 파이프라인이 각 카테고리를 **서로 다른 패스**에서 처리해야 하기 때문이다.

| 메시           | 대상 블록           | 렌더 패스                          | 왜 별도인가                                                                                              |
| -------------- | ------------------- | ---------------------------------- | -------------------------------------------------------------------------------------------------------- |
| **LowLod**     | Opaque + SemiAlpha  | 원거리 LOD 패스                    | 먼 청크는 잎/나무 등 반투명 처리를 생략해 불투명으로 통합 렌더. 드로우 콜과 오버드로우를 크게 줄인다.    |
| **Opaque**     | 불투명 블록         | G-Buffer 채우기 (Deferred Shading) | 표준 지연 렌더. 깊이 테스트만 있으면 되고, 알파 관련 로직이 없어 셰이더가 가장 단순하고 빠르다.          |
| **SemiAlpha**  | 잎, 유리 등         | G-Buffer 채우기 (Alpha Test)       | 완전 투명도 완전 불투명도 아닌 픽셀. Deferred에 넣되 알파 테스트로 조기 discard.                          |
| **Transparency** | 물 등             | Forward 투명 패스 (Alpha Blend)    | Deferred는 알파 블렌드가 곤란하므로 Forward로 뺀다. 반사·굴절·투과 색 등 water shader만의 처리가 필요.  |

같은 청크에서 나온 메시더라도 사용하는 셰이더/블렌드 상태/렌더 타깃이 다르기 때문에, 하나의 큰 버퍼에 섞어 두면 매 드로우 콜마다 상태 전환 비용이 발생한다. **CPU 단계에서 미리 4종으로 분리해 두면 GPU 업로드 이후에는 카테고리별로 한 번씩만 바인딩하고 그리면 된다.**

또한 Face Culling 규칙도 카테고리마다 다르다.
- Opaque/LowLod: 인접 블록이 동일 카테고리이면 컬링 (양방향)
- Transparency: 인접 블록이 자신과 다른 투명·반투명 블록일 때만 그림
- SemiAlpha: 방향별로 규칙이 갈리는 하이브리드 (컬링 조건이 축 방향에 따라 다름)

컬링 규칙이 다르다는 것은 초기 컬럼 비트 배열부터 분리해야 한다는 의미이고, 이 자체가 4종 분리를 강제하는 요인이다.

## 5. GPU 버퍼는 왜 Chunk가 아니라 ChunkManager가 소유하는가

Chunk는 CPU 메시 데이터(`std::vector<VoxelVertex>`, `std::vector<uint32_t>`)만 소유한다. GPU에 올라가는 `ID3D11Buffer`는 **모두 ChunkManager**가 보유한다.

```cpp
// ChunkManager
std::vector<ComPtr<ID3D11Buffer>> m_opaqueVertexBuffers;       // [CHUNK_POOL_SIZE]
std::vector<ComPtr<ID3D11Buffer>> m_opaqueIndexBuffers;
std::vector<ComPtr<ID3D11Buffer>> m_transparencyVertexBuffers;
// ... (4종 각각 + constant + instance)
```

각 배열은 `CHUNK_POOL_SIZE` 크기로 사전 할당되고, Chunk의 `m_id`가 그대로 인덱스로 쓰인다. 청크가 풀에서 재활용되면 같은 슬롯의 GPU 버퍼도 재사용된다 (필요 시 `ResizeBuffer`, 아니면 `UpdateBuffer`로 덮어쓰기).

### 왜 Chunk에 두지 않았는가

원래 자연스러운 설계는 "각 Chunk가 자기 GPU 버퍼를 들고 있는" 것이다. 실제로 프로젝트 초기에는 그랬다. 이후 다음 흐름을 거쳐 지금 구조가 되었다.

1. **Instance 렌더링 도입**: 풀·꽃 등은 청크마다 그리는 게 아니라 형상 타입(Cross/Fence/Square/Floor)별로 인스턴스 정보를 모아 `DrawIndexedInstanced`로 한 번에 그린다. 즉 인스턴스 GPU 버퍼는 **청크의 소유물이 아니라 렌더 시스템의 공유 자원**이 되어야 했고, 자연스럽게 ChunkManager로 들어갔다.
2. **통일성 문제**: 이제 GPU 버퍼가 Instance는 Manager, Block은 Chunk에 있는 이원 구조가 되어버렸다. 리사이즈/업로드 코드가 두 위치로 갈라지고, DX11 디바이스/컨텍스트 접근 경로도 어긋났다.
3. **결정**: 통일성을 위해 Block 관련 GPU 버퍼도 ChunkManager로 이동. 그 결과 **Chunk는 CPU 데이터만 다루는 순수한 데이터 홀더**가 되었고, GPU 리소스 라이프사이클은 매니저 한 곳에서 관리된다.

### 얻은 이점

- **DX11 Immediate Context 단일 스레드 제약과의 호환**: 워커 스레드는 Chunk의 CPU 데이터만 채우고, GPU 업로드는 매니저가 메인 스레드에서 수행한다. Chunk 자체가 D3D 리소스를 직접 다루지 않으므로 스레드 경계가 명확해진다.
- **풀 인덱스 기반 리소스 재사용**: `m_id`가 곧 GPU 버퍼 인덱스라, 청크가 언로드-재로드 되어도 같은 슬롯을 그대로 재사용한다. 매 재로드마다 D3D11 리소스를 새로 만들지 않아도 된다.
- **인스턴스와 블록의 코드 경로 통일**: `DXUtils::ResizeBuffer`/`UpdateBuffer` 같은 헬퍼가 두 카테고리에 동일하게 쓰인다.

## 6. Chunk::Update - 등장 애니메이션

`Chunk::Update`가 하는 일은 매우 좁다: **새로 로드된 청크가 아래에서 위로 솟아오르는 애니메이션**이다.

```cpp
void Chunk::Update(float dt)
{
    if (m_isUpdateRequired) {
        m_position.y += 50.0f * dt;
        if (m_position.y > m_offsetPosition.y) {
            m_position.y = m_offsetPosition.y;
            m_isUpdateRequired = false;
        }
        m_constantData.world = Matrix::CreateTranslation(m_position);
    }
}
```

- Initialize 종료 시 `m_position.y`는 `-2 * CHUNK_SIZE`로 세팅된다 (청크 두 개 아래에서 시작).
- 매 프레임 `m_position.y`가 `m_offsetPosition.y`까지 상승하고, 도달하면 애니메이션 상태(`m_isUpdateRequired`)를 해제한다.
- 갱신된 world 행렬은 `m_constantData`에 반영되며, 다음 프레임 ChunkManager가 GPU 상수 버퍼로 업로드한다.

블록 데이터의 변경(패치)은 별도 경로(`Chunk::Patch`, 워커 스레드)에서 다뤄지므로 여기서는 관여하지 않는다.

## 7. Clear - 풀 반환 준비

청크가 언로드되어 Object Pool로 반환되기 직전에 호출된다. 다음 슬롯 재사용을 위해 상태를 초기화한다.

```
m_isLoaded / m_isPatching / m_isUpdateRequired / m_onPatchDirtyFlag = false
m_instanceMap.clear()
CPU 버텍스/인덱스 4종 clear
UpdateCpuBufferCount()   // 카운트도 0으로 스냅샷
```

`m_id`와 `m_blocks` 배열은 그대로 남는다. `m_blocks`는 다음 Initialize에서 `CHUNK_SIZE_P³` 전 지점이 덮어써지므로 별도 초기화가 필요 없다. GPU 버퍼는 ChunkManager가 소유하므로 여기서 손대지 않는다.

## 8. 회고

- **Block이 `BLOCK_TYPE(uint8_t)` 한 필드**라 메모리는 매우 절약되지만, 조명 레벨·손상도 같은 추가 상태를 도입하려면 자료구조 변경이 강제된다. 비트 패킹 확장 또는 별도 병렬 배열이 자연스러운 다음 단계다.
- **`m_instanceMap`이 `unordered_map` 기반**이라 밀집 바이옴(숲, 늪)에서 해시 오버헤드가 나타날 여지가 있다. 현재는 `MAX_INSTANCE_BUFFER_SIZE(8MB)` 제한으로 상한을 두고 있다.
- **패치 시 메시 4종을 전체 재생성**하는 것은 구현이 단순하지만 블록 하나 변경에 32³ 범위의 Greedy Meshing을 다시 수행한다는 뜻이다. 변경된 블록 주변만 부분 재생성하는 방식이 명확한 개선 방향이다.
- **등장 애니메이션 상수(50 units/s, `-2 * CHUNK_SIZE` 시작)** 가 하드코딩되어 있어 튜닝이 곤란하다. 프레임 스파이크가 눈에 띌 정도로 로드가 몰릴 때 시각적 완충 역할을 하는 만큼, 프레임률/로드 상황에 반응하는 동적 스케줄로 개선할 수 있다.
