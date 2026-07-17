# Instance System

<br />

## 1. 개요

풀, 꽃, 양치, 덩굴, 켈프, 수련처럼 블록(1×1×1)과 **다른 메쉬를 가지는 세밀한 오브젝트**를 Instance로 구분하였다.

청크 로드 시 지형과 트리가 놓인 후 Instance를 배치하고 이 Instance는 ChunkManager에서 정보를 모아 Cross / Fence / Square / Floor 4가지 quad 프리미티브 중 하나로 그려진다.

## 2. Instance 구조 — Instance → InstanceTypeInfoSet → InstanceTypeInfo

[Biome](../Biome/README.md), [Block](../Block/README.md), [Tree](../Tree/README.md) 와 동일한 패턴을 사용하지만,
**Instance** 자체가 1개의 속성이 아닌 여러 개의 속성을 가진다는 점은 다르다.

- `Instance` — **정적 함수만 노출하는 접근 계층** + 개별 인스턴스는 **좌표별 상태를 5개 필드로 갖는 값 객체**
- `InstanceTypeInfoSet` — 256개의 `InstanceTypeInfo`를 담는 컨테이너, 생성자에서 각 타입의 속성을 세팅
- `InstanceTypeInfo` — 한 인스턴스 정보를 담는 객체: 프리미티브 종류 타입, 텍스쳐 인덱스, 다층 인스턴스 텍스처 인덱스, 최대 높이

```cpp
// voxen/headers/Instance.h  (요약)
class Instance {
public:
    static const uint32_t INSTANCE_TYPE_COUNT = 256;

    static INSTANCE_SHAPE   GetShape                 (INSTANCE_TYPE);
    static TEXTURE_INDEX    GetTextureIndex          (INSTANCE_TYPE);
    static TEXTURE_INDEX    GetTextureTopIndex       (INSTANCE_TYPE);
    static TEXTURE_INDEX    GetTextureBottomIndex    (INSTANCE_TYPE);
    static TEXTURE_INDEX    GetTextureIndexByHeight  (INSTANCE_TYPE, int curH, int maxH);
    static uint8_t          GetMaxHeight             (INSTANCE_TYPE);
    static INSTANCE_TYPE    GetInstanceTypeForBiome  (BIOME_TYPE, float d, PosInt3);
    static INSTANCE_TYPE    GetInstanceTypeForWaterPlane(float t, float h, float d);
    static bool             CanPlace(INSTANCE_TYPE, BLOCK_TYPE cur, BLOCK_TYPE bottom);

    // 인스턴스 상태 5개 필드
    Instance(INSTANCE_TYPE type, TEXTURE_INDEX texIndex, float yaw, Vector2 offset, uint8_t faceFlag);
    ...
private:
    static InstanceTypeInfoSet m_instanceTypeInfoSet;

    INSTANCE_TYPE m_type;                    // grass, tulip, kelp, ...
    TEXTURE_INDEX m_texIndex;                // 실제 렌더링에 쓰는 텍스처 (다층 시 층별로 다름)
    float         m_yawRotation;             // Y 축 회전 각 [0, 360)
    Vector2       m_offsetNoisePositionXZ;   // 셀 중심에서 XZ 랜덤 오프셋 (-0.25~+0.25)
    uint8_t       m_faceFlag;                // vine 방향 비트 (일반 인스턴스는 0)
};
```

`Instance::GetShape(type)` 등은 아래처럼 정적 테이블로 위임된다.

```cpp
// voxen/srcs/Instance.cpp
InstanceTypeInfoSet Instance::m_instanceTypeInfoSet;

INSTANCE_SHAPE Instance::GetShape(INSTANCE_TYPE type)
{
    return m_instanceTypeInfoSet.GetInfo(type).GetShape();
}
```

### 2.1 왜 OOP 를 쓰지 않았는가

앞선 Biome / Block / Tree 문서와 동일한 이유다.

요약: 로직이 다르기보단, 상태 값이 다르다. 구성 로직이 다른 모양 결정은 충분히 switch-case나 if로 구성을 한 곳에 할 수 있다.

### 2.2 4개 shape 종류의 성격을 가지는 Instance

이는 Instance Rendering 시 Vertex Buffer를 다르게 두기 위함이다.

- SQUARE와 FLOOR는 같은 단일 Quad 이지만, 회전에 다른 예외를 주기 싫어서 따로 구분하였다.

```cpp
// voxen/headers/Enums.h
enum INSTANCE_SHAPE : uint8_t {
    INSTANCE_CROSS  = 0,
    INSTANCE_FENCE  = 1,
    INSTANCE_SQUARE = 2,
    INSTANCE_FLOOR  = 3,
};
```

| shape      | 형태                                 | 사용 예                                          |
| ---------- | ------------------------------------ | ------------------------------------------------ |
| **CROSS**  | X 자로 교차한 2개의 quad             | 대부분 (grass, tulip, fern, mushroom …)          |
| **FENCE**  | +를 두개 교차한 4개의 quad           | 볼륨감이 있는 것 (sweet berry bush, rose plants) |
| **SQUARE** | 벽에 붙는 단일 quad (face flag 사용) | vine (트리에서 뻗음)                             |
| **FLOOR**  | 지면에 평평하게 눕는 단일 quad       | water lily                                       |

### 2.3 등록된 인스턴스 타입 목록

```cpp
// voxen/headers/Instance.h  InstanceTypeInfoSet 생성자에서 등록되는 항목
GRASS       (CROSS, short/double grass 텍스처, maxHeight 2)
SEAGRASS    (CROSS, single/double seagrass,     maxHeight 2)
KELP        (CROSS, kelp top/body,              maxHeight 10)  ← 가장 큰 다층
FERN        (CROSS, single/double fern,         maxHeight 3)
...
SWEET_BERRY_BUSH (FENCE, ...)
ROSE_PLANTS      (FENCE, ...)
WATER_LILY       (FLOOR, ...)
VINE             (SQUARE, ..., faceFlag 로 방향 지정)

OXEYE_DAISY / CORN_FLOWER / TULIP_PINK / TULIP_RED / TULIP_WHITE / TULIP_ORANGE
BLUE_ORCHID / MUSHROOM_BROWN / MUSHROOM_RED / DEAD_BUSH
ROSE_BLUE / ROSE_RED / LILY_OF_THE_VALLEY / ALLIUM / DANDELION
   (모두 CROSS, 단층 maxHeight 1)
```

## 3. Instance 심기 파이프라인 — `Chunk::InitInstancePlace`

`Chunk::Initialize` 의 5번째 단계 (Tree 다음).

```
InitTerrainNoises → InitBiomeMapAndCount → InitBasicBlockType → InitTreePlace
                                                                       ↓
                                                              InitInstancePlace   ← 이 문서
                                                                       ↓
                                                              InitWorldVerticesData
```

전체 흐름:

```cpp
// voxen/srcs/Chunk.cpp
void Chunk::InitInstancePlace(ChunkLoadMemory* memory)
{
    // (1) 랜덤 xz 후보 뽑기 — 청크당 최대 256개
    Terrain::GenerateRandomPlace2D(m_offsetPosition,
        INSTANCE_PLACE_RANDOM_SOLT_X, INSTANCE_PLACE_RANDOM_SOLT_Z,
        INSTANCE_PLACE_MAX_COUNT_PER_CHUNK, CHUNK_SIZE,
        memory->instanceRandomPlace2D);

    uint32_t placedBiomeInstanceCount[BIOME_TYPE::BIOME_COUNT] = { 0, };

    for (int i = 0; i < INSTANCE_PLACE_MAX_COUNT_PER_CHUNK; ++i) {
        int x = memory->instanceRandomPlace2D[i].first;
        int z = memory->instanceRandomPlace2D[i].second;

        // (2) 물 위 인스턴스 (수련) 먼저 시도 — y = 64 청크에서만
        if (CanPlaceWaterPlaneInstanceAt(x, z, memory)) {
            INSTANCE_TYPE instanceType = GetWaterPlaneInstanceType(x, z, memory);
            SetWaterPlaneInstance(x, z, instanceType, memory);
        }

        // (3) biome 인스턴스 (풀/꽃/양치/덩굴...)
        BIOME_TYPE biomeType = memory->biomeMap2D[x + 1][z + 1];
        float elevationWorldY = std::ceil(memory->elevationNoises[x + 1][z + 1]);
        int   localY = (int)(elevationWorldY - m_offsetPosition.y);

        if (CanPlaceBiomeInstanceAt(x, localY, z, placedBiomeInstanceCount[biomeType], memory)) {
            INSTANCE_TYPE instanceType = GetBiomeInstanceType(x, localY, z, memory);
            SetBiomeInstance(x, localY, z, instanceType, memory);
            placedBiomeInstanceCount[biomeType]++;
        }
    }
}
```

Tree 파이프라인과 동일한 구조지만 **water plane** (수련) 이 앞에 별도 브랜치로 존재하는
것이 특징이다. 수련은 물 표면 (`y=64`) 청크에서만 후보가 되어 별도 조건 (`humidity > 0.77
&& temperature > 0.5 && distribution > 0.75`) 을 만족할 때 놓이며, 이후 같은 좌표에서
biome 인스턴스가 다시 시도된다 (두 인스턴스가 y 좌표가 다르므로 충돌하지 않음).

### 3.1 랜덤 위치 선정

Tree 와 완전히 동일한 `Terrain::GenerateRandomPlace2D` 를 사용하지만
**서로 다른 solt (`INSTANCE_PLACE_RANDOM_SOLT_X = 405071179U`,
`INSTANCE_PLACE_RANDOM_SOLT_Z = 397760329U`)** 를 주어 완전히 다른 랜덤 시퀀스를 얻는다.
그래서 트리 위치와 인스턴스 위치가 상관성이 없다.

`INSTANCE_PLACE_MAX_COUNT_PER_CHUNK = CHUNK_SIZE² / 4 = 256` 은 청크 표면 (32×32 = 1024) 의
1/4 정도를 후보로 뽑는 값이다. 실제 배치되는 개수는 biome quota 로 제한.

### 3.2 인스턴스 가능 검사 — 두 종류

#### (a) `CanPlaceWaterPlaneInstanceAt` — 수련 조건

```cpp
bool Chunk::CanPlaceWaterPlaneInstanceAt(int x, int z, ChunkLoadMemory* memory)
{
    const float WATER_HEIGHT = 64.0f;

    if (m_offsetPosition.y != WATER_HEIGHT) return false;                 // [A] y=64 청크만
    if (elevationWorldY   >= WATER_HEIGHT)  return false;                 // [B] 지형이 수면 위면 X
    if (GetWaterPlaneInstanceType(x,z,mem) == INSTANCE_NONE) return false; // [C] t/h/d 조건 불충족
    return true;
}
```

- **[A]** 청크 y offset 이 64 (수면 높이) 인 청크에만 있는 검사.
  청크는 y 축으로 -64, -32, 0, 32, 64, ... 처럼 32 단위로 배치되므로 y=64 청크는 수면을 정확히 포함하는 하나의 층.
- **[B] `elevationWorldY >= 64`** 이면 이 xz 는 육지라서 수련 놓을 물이 없음.
- **[C]** 수련은 `Instance::GetInstanceTypeForWaterPlane` 이 결정. 실제 로직은:

```cpp
INSTANCE_TYPE Instance::GetInstanceTypeForWaterPlane(float t, float h, float d)
{
    if (h > 0.77f && t > 0.5f && d > 0.75f)  return INSTANCE_TYPE::INSTANCE_WATER_LILY;
    return INSTANCE_TYPE::INSTANCE_NONE;
}
```

즉 매우 습하고 따뜻한 (열대성) 지역에만 수련이 뜬다. distribution 조건이 있어 국소적으로 뿌려진다.

#### (b) `CanPlaceBiomeInstanceAt` — 일반 지상 인스턴스

Tree 의 `CanPlaceTreeAt` 와 거의 같은 4단계 필터.

```cpp
bool Chunk::CanPlaceBiomeInstanceAt(int x, int y, int z, uint32_t placedCount, ...)
{
    // [A] biome 별 quota — biome 이 청크를 얼마나 차지하는지에 비례
    if (placedCount >= GetMaxPlaceCountByBiomeRatio(
                          Biome::GetMaxInstanceCountPerChunk(biomeType),
                          memory->biomeCount[biomeType])) return false;

    // [B] 청크 내부여야 함
    if (!IsInsideChunk(x, y, z))                                 return false;

    // [C] 같은 위치에 다른 인스턴스가 이미 있으면 X (수련 vs 지상 겹침 방지)
    if (IsInstanceAt(x, y, z))                                   return false;

    // [D] 이 biome 이 이 좌표에 어떤 인스턴스를 갖는지 결정 후 unset 이면 X
    INSTANCE_TYPE type = GetBiomeInstanceType(x, y, z, memory);
    if (type == INSTANCE_TYPE::INSTANCE_NONE)                    return false;

    // [E] shape / block 결합 조건 (Instance::CanPlace)
    return CheckInstancePlaceCondition(type, x, y, z);
}
```

[E] 의 `Instance::CanPlace(type, currentBlock, bottomBlock)` 는 인스턴스가 놓일 셀 하나와 그 아래 셀만 보는 로컬 규칙이다.

```cpp
bool Instance::CanPlace(INSTANCE_TYPE type, BLOCK_TYPE cur, BLOCK_TYPE bottom)
{
    if (!Block::IsTransparency(cur))                             return false; // 셀은 공기/물이어야
    if (type == INSTANCE_KELP && cur != BLOCK_WATER)             return false; // 켈프는 물 안에만
    if (type == INSTANCE_WATER_LILY)                                            // 수련은 물 위 공기
        return (cur == BLOCK_AIR && bottom == BLOCK_WATER);

    // 지상 인스턴스는 아래가 흙/잔디/설잔디/자갈 중 하나
    if (bottom == BLOCK_DIRT || bottom == BLOCK_GRASS ||
        bottom == BLOCK_SNOW_GRASS || bottom == BLOCK_GRAVEL)   return true;

    // dead bush 는 모래 위 허용
    if (INSTANCE_TYPE::INSTANCE_DEAD_BUSH && bottom == BLOCK_SAND) return true;
    return false;
}
```

### 3.3 인스턴스 종류 결정 — `Instance::GetInstanceTypeForBiome`

Tree 의 `GetTreeTypeForBiome` 와 완전히 같은 패턴이다. Biome 의 `Instances` 후보 목록에서 `d` (distribution) 값과 좌표 hash 로 하나를 선택.

```cpp
// voxen/srcs/Instance.cpp
INSTANCE_TYPE Instance::GetInstanceTypeForBiome(BIOME_TYPE biomeType, float d, PosInt3 worldPos)
{
    const std::vector<INSTANCE_TYPE>& biomeInstances = Biome::GetInstances(biomeType);
    int worldY = std::get<1>(worldPos);

    switch (biomeType) {
    case BIOME_OCEAN:
        // 얕은 물(<52)에서 seagrass, 깊으면 kelp
        return (worldY < 52 && d < 0.5f) ? biomeInstances[0] : biomeInstances[1];

    case BIOME_PLAINS:
        if      (d < 0.70f) return biomeInstances[0];                              // grass
        else if (d < 0.83f) return biomeInstances[Utils::RandomRangeByPos(worldPos, 1, 2)];  // daisy/cornflower
        else                return biomeInstances[Utils::RandomRangeByPos(worldPos, 3, 6)];  // 4종 tulip
    ...
    }
}
```

- `d` 로 **큰 범주 (풀 / 꽃)** 를 먼저 나누고,
- 꽃 범주 안에서는 `Utils::RandomRangeByPos(worldPos, ...)` 좌표 hash 로 **어떤 꽃 variant** 인지를 결정.
- 결과적으로 큰 지역에서 grass 가 지배적이고, 국소 spot 에서 각기 다른 꽃들이 섞인 자연스러운 패턴이 만들어진다.

### 3.4 인스턴스 심기 — `SetBiomeInstance` / `SetWaterPlaneInstance`

각 인스턴스는 배치 시 좌표 hash 로 yaw 회전과 xz 오프셋을 뽑아 **같은 종이라도 미묘하게 다르게** 보이도록 한다.

```cpp
void Chunk::SetBiomeInstance(int x, int y, int z, INSTANCE_TYPE type, ...)
{
    PosInt3 worldPosInt3 = ...;
    float rangeRotation  = Utils::RandomRangeByPos(worldPosInt3, 0, 360);              // 회전
    float ofsX = (Utils::RandomRangeByPos(worldPosInt3, 0, 50) - 25) / 100.0f;         // XZ 오프셋 -0.25 ~ +0.25
    float ofsZ = (Utils::RandomRangeByPos(worldPosInt3, 0, 50) - 25) / 100.0f;
    Vector2 rangeOffsetNoiseXZ = { ofsX, ofsZ };

    // 다층 인스턴스 처리 (kelp, grass, fern...)
    int instanceMaxH = Instance::GetMaxHeight(type);
    float thPercent  = (temperature + humidity) * 0.5f;
    int thMaxH       = (int)std::ceil(thPercent * instanceMaxH);
    int rangeHeight  = Utils::RandomRangeByPos(worldPosInt3, 1, thMaxH);

    for (int h = 0; h < rangeHeight; ++h) {
        TEXTURE_INDEX tex = Instance::GetTextureIndexByHeight(type, h + 1, rangeHeight);
        Instance instance = Instance(type, tex, rangeRotation, rangeOffsetNoiseXZ);

        if (IsInsideChunk(x, y + h, z)) {
            m_instanceMap.insert({ PosInt3(x, y + h, z), instance });
        }
        else {
            // 다층이 청크 y 경계를 넘어감 → 이웃 청크로 patch
            PatchData patchData(x, y + h, z, Block(), instance, CHUNK_SIZE, true);
            memory->loadPatchResult[ownerOffset].insert(patchData);
        }
    }
}
```

세 가지 포인트:

- **`yaw` 회전 [0, 360)** 과 **`offset` [-0.25, +0.25]** 로 같은 grass 여러 개가 반복돼도 정면 정렬돼 보이지 않는다. offset 은 CPU 에서 계산하고 GPU 인스턴스 상수로 넘김.
- **다층 인스턴스 (grass 2, fern 3, kelp 10)** 는 세로로 여러 셀을 차지한다.
  `thMaxH = ceil((t + h) / 2 * maxHeight)` 로 온도·습도가 높을수록 더 크게 자라도록 조절되고,
  실제 층 수는 그 안에서 좌표 hash 랜덤. Kelp 는 물의 깊이 안에서 최대 10칸까지 자란다.
- **`GetTextureIndexByHeight(type, h+1, rangeHeight)`** 는 층별로 다른 텍스처를 반환.
  grass 는 (bottom, top) 두 텍스처가 있어서 2단이면 아래-위가 다르게 그려진다.

`SetWaterPlaneInstance` 는 단층이고 y = 0 (수련은 물 표면에 한 층만) 이라 다층 처리가 없다.

## 4. 주변 청크에 미치는 영향

Instance 는 Tree 와 달리 xz 로는 **한 셀만 차지**한다. 따라서 xz 방향 청크 경계 문제는 없다.
반면 **다층 인스턴스가 청크 y 경계를 넘는 경우** 에는 트리와 동일하게 patch 전파가 필요하다.

### 4.1 y 경계 patch — 다층 인스턴스만 해당

`SetBiomeInstance` 의 for 문 마지막 else 브랜치가 이 케이스다.

```cpp
if (IsInsideChunk(x, y + h, z)) {
    m_instanceMap.insert(...);                                       // 내 청크
} else {
    PatchData patchData(x, y + h, z, Block(), instance, CHUNK_SIZE, true);
    Vector3 instancePos = m_offsetPosition + Vector3(x, y + h, z);
    Vector3 ownerOffset = Utils::CalcOffsetPos(instancePos, CHUNK_SIZE);
    memory->loadPatchResult[ownerOffsetInt3].insert(patchData);      // 이웃 청크
}
```

- 다층 인스턴스가 청크 상단 (y ≈ 31) 근처에 심어지면, 위쪽 몇 층은 이웃 청크(위 청크) 로 patch.
- kelp (maxHeight 10) 가 청크 하단 근처 물속에 심어지면 아래 청크로 갈 수도 있고 (실제로는 여기선 y = elevation 위쪽이라 위 방향만 넘어감).
- Tree 문서와 동일하게 `loadPatchResult` 는 청크 로드 완료 후 `ChunkManager` 가 모아서 [Patch 파이프라인](../../ChunkManagement/README.md) 으로 넘긴다.

### 4.2 xz 경계는 불필요

- 인스턴스는 xz 로 한 셀이라 인접 xz 청크에는 절대 영향을 주지 않음.
- 심지어 mesh 도 인스턴스는 **instanced rendering (per-cell 인스턴스 상수 버퍼)** 로 그리므로,
  블록의 greedy meshing 처럼 seam 이 생기지 않는다.
  즉 트리처럼 `IsInnerEdge / IsOuterEdge` 로 인접 청크에 seam 방지 patch 를 보낼 필요가 없다.

### 4.3 트리에서 뻗어나온 vine 은 이미 처리됨

Tree 문서에서 다뤘듯이 vine 은 **Tree 파이프라인에서 `Instance` 로 만들어 저장**되며,
청크 경계를 넘으면 그때 이미 patch 로 처리된다. `InitInstancePlace` 는 vine 을 새로 만들지 않으므로 여기서 다시 처리할 필요가 없다.

## 5. 렌더링 — Instanced Rendering

블록과 달리 인스턴스는 **한 셀에 quad 몇 개** 밖에 되지 않고 위치·회전·텍스처만 조금씩 다른
같은 mesh 가 청크에 수백~수천 개 뿌려진다. 이 특성에 맞게 D3D11 의
`DrawIndexedInstanced` 를 사용해 **하나의 base mesh 를 인스턴스 데이터 배열과 함께
한 번의 draw call 로 그린다.**

### 5.1 두 개의 vertex buffer — Per-Vertex + Per-Instance

인스턴스 렌더링은 두 가지 데이터를 각각 다른 rate 로 IA (Input Assembler) 에 흘려넣는다.

```cpp
// voxen/headers/Structure.h
struct InstanceVertex {          // per-vertex : shape 하나당 한 벌만 존재
    Vector3 position;
    Vector3 normal;
    Vector2 texcoord;
};

struct InstanceInfoVertex {      // per-instance : 한 인스턴스마다 하나
    Matrix   instanceWorld;      // 셀 위치 + yaw 회전 + 오프셋을 합친 world matrix
    uint32_t texIndex;           // 텍스처 아틀라스 인덱스
};
```

- **`InstanceVertex`** — 한 shape (Cross / Fence / Square / Floor) 의 quad geometry.
  Cross 는 8 verts (X 자 quad 2개), Fence 는 16 verts (+ 자 quad 4개), Square/Floor 는 4 verts.
  이 데이터는 **앱 시작 시 한 번만 생성** 되고 이후 변하지 않는다.
- **`InstanceInfoVertex`** — 좌표별 인스턴스 상태.
  [1.1](#11-자료구조--instance--instancetypeinfoset--instancetypeinfo) 에서 다룬
  `Instance` 값 객체 (`m_yawRotation`, `m_offsetNoisePositionXZ`, `m_texIndex`, 위치) 를
  **셰이더가 읽기 편한 형태 (world matrix + texIndex)** 로 변환한 것.
  매 프레임 render chunk list 순회하며 채워진다.

### 5.2 앱 시작 시 — `MakeInstanceVertexBuffer` (base mesh 4 종)

Shape 4개 각각에 대해 base mesh 를 만들고 GPU 정적 버퍼로 올려 둔다.

```cpp
// MeshGenerator.h  (호출부는 ChunkManager::MakeInstanceVertexBuffer)
MeshGenerator::CreateCrossInstanceMesh (verts, indices);   // 8 verts, 12 indices
MeshGenerator::CreateFenceInstanceMesh (verts, indices);   // 16 verts, 24 indices
MeshGenerator::CreateSquareInstanceMesh(verts, indices);   // 4 verts,  6 indices  (vine)
MeshGenerator::CreateFloorInstanceMesh (verts, indices);   // 4 verts,  6 indices  (water lily)
```

`ChunkManager` 는 `m_instanceVertexBuffers[4]` / `m_instanceIndexBuffers[4]` 로 이 4개 세트를 보관한다.
정적 데이터라 이후에는 절대 재업로드하지 않는다.

### 5.3 매 프레임 — `UpdateInstanceInfoList` (ChunkManager 가 한 번에 수집)

`ChunkManager::Update()` 안에서 매 프레임 호출된다.

```cpp
// voxen/srcs/ChunkManager.cpp
void ChunkManager::UpdateInstanceInfoList(Camera& camera)
{
    // (1) shape 별 임시 리스트 초기화
    for (int i = 0; i < INSTANCE_SHAPE_COUNT; ++i)
        m_instanceInfoList[i].clear();

    // (2) 렌더 대상 청크만 훑기
    for (auto& c : m_renderChunkList) {
        if (c->IsUpdateRequired()) continue;                                 // 로딩 중이면 skip
        if (chunkCenterDistance > Camera::LOD_RENDER_DISTANCE) continue;     // 원거리 LOD 청크는 인스턴스 안 그림

        // (3) 각 청크의 m_instanceMap 을 shape 별 buffer 에 뿌림
        for (auto& [localPos, instance] : c->GetInstanceMap()) {
            Vector3 worldPosition = c->GetOffsetPosition() + PosInt3ToVector(localPos);
            if (instance.GetFaceFlag() > 0)                                  // vine 은 방향별 quad split
                AddInstanceInfoBySplitFace(worldPosition, instance);
            else
                AddInstanceInfo(worldPosition, instance);                    // 일반: shape 하나에 push
        }
    }

    // (4) shape 4개 각각의 GPU 버퍼로 한 번씩 upload
    for (int i = 0; i < INSTANCE_SHAPE_COUNT; ++i) {
        DXUtils::ResizeBuffer(m_instanceInfoBuffers[i], m_instanceInfoList[i],
                              D3D11_BIND_VERTEX_BUFFER, m_instanceInfoList[i].size() + 1024);
        DXUtils::UpdateBuffer (m_instanceInfoBuffers[i], m_instanceInfoList[i]);
    }
}
```

핵심 포인트:

- 인스턴스 데이터는 **청크 각자가 아니라 `ChunkManager` 가 한 큰 배열로 모아서 관리**한다.
  청크마다 별도 GPU 인스턴스 버퍼를 두면 프레임당 draw call 수가 청크 수 × shape 수로 폭발한다.
- 시야 밖 / 로딩 중 / 원거리 청크는 여기서 필터링되므로 GPU 로 올라가는 데이터가 그만큼 줄어든다.
- `AddInstanceInfo` 는 `instance` 하나로부터 `InstanceInfoVertex` 하나를 만들어 (world = translate·yaw·offset, texIndex 그대로) 해당 shape 배열에 push.
- Vine 은 방향 flag (`V_LEFT / V_RIGHT / V_FRONT / V_BACK`) 마다 벽 면이 다르므로 `AddInstanceInfoBySplitFace` 로 방향별로 여러 개의 `InstanceInfoVertex` 를 만들어 넣는다.
- 상한은 `MAX_INSTANCE_BUFFER_SIZE = 8 MB` → `InstanceInfoVertex` (약 68 byte) 로 나누면 shape 당 약 **12만 인스턴스**.

### 5.4 Draw — `RenderInstance` (shape 4 개 × 각 1 draw call)

```cpp
void ChunkManager::RenderInstance()
{
    UINT indexCountPerInstance[INSTANCE_SHAPE_COUNT] = { 12, 24, 6, 6 };  // Cross/Fence/Square/Floor

    for (int i = 0; i < INSTANCE_SHAPE_COUNT; ++i) {
        // per-vertex + per-instance 두 버퍼를 slot 0, 1 에 바인딩
        std::vector<UINT> strides = { sizeof(InstanceVertex), sizeof(InstanceInfoVertex) };
        ID3D11Buffer* buffers[]  = { m_instanceVertexBuffers[i].Get(),
                                     m_instanceInfoBuffers  [i].Get() };
        context->IASetVertexBuffers(0, 2, buffers, strides, offsets);
        context->IASetIndexBuffer  (m_instanceIndexBuffers[i].Get(), DXGI_FORMAT_R32_UINT, 0);

        // 이 shape 에 속한 모든 인스턴스를 한 번에 그림
        context->DrawIndexedInstanced(indexCountPerInstance[i],
                                      m_instanceInfoList[i].size(),
                                      0, 0, 0);
    }
}
```

- 프레임당 총 draw call 은 **shape 개수 만큼 (= 4 회)** 이 된다.
  청크 수나 실제 인스턴스 개수와 무관.
- 셰이더에서는 IA slot 0 에서 정점 데이터 (`InstanceVertex.position, normal, texcoord`) 를,
  slot 1 에서 인스턴스 데이터 (`instanceWorld`, `texIndex`) 를 받아
  `mul(position, instanceWorld)` 로 각 인스턴스의 world 위치로 변환한 뒤
  `texIndex` 로 텍스처 아틀라스에서 원하는 텍스처를 샘플링한다.
- `RenderInstance` 는 서로 다른 PSO 로 두 번 호출된다:
  Basic pass (`Graphics::instancePSO`) 와 Mirror world pass (`Graphics::instanceMirrorPSO`).
  둘 다 같은 GPU 버퍼를 재사용.

## 6. 회고

- **정적 속성은 테이블, 좌표별 상태는 값 객체 필드** 라는 분리가 Block/Tree 와 같은
  패턴이면서도 Instance 는 `yaw`/`offset`/`faceFlag` 같은 좌표별 상태가 있어야 해서
  값 객체가 조금 더 두꺼워졌다. 이 필드들이 그대로 GPU 인스턴스 버퍼에 흘러들어가므로
  구조상 자연스럽다.

- **다층 인스턴스 y-patch 처리** 가 트리와 완전히 같은 함수를 재사용한다.
  Patch 시스템 하나가 트리(대형)/인스턴스(다층)/vine(방향) 을 모두 통일된 방식으로
  처리하는 것이 확장성 면에서 좋았다. 나중에 "3층 짜리 산호" 같은 걸 추가하더라도
  같은 경로를 탈 것이다.

- **아쉬운 점** : `GetInstanceTypeForBiome` 이 switch/case 로 12개 biome 을 나열하고 있어
  Biome 추가 시 여기와 `GetTreeTypeForBiome`, `GetBlockTypeForBiome` 세 곳을 동시에 수정해야 한다.
  Biome 마다 `(distribution → instance)` 결정 규칙을 데이터로 등록하는 방식으로 옮기면 추가가 한 파일에서 완결.

- **개선하고 싶은 방향** : 현재 다층 높이 결정이
  `rangeHeight = RandomRangeByPos(worldPos, 1, thMaxHeight)` 라 최소 1층에서 시작한다.
  Kelp 는 물속에서 자라므로 물의 실제 깊이 (해수면 - 지형) 를 상한으로 두어야 자연스러운데 지금은
  물이 얕은 지역에서도 kelp 가 최대 10층까지 자랄 여지가 있다. `CanPlace` 단계에서 물 깊이 검사가 추가되면 좋을 것.
