# Instance System

<img width="718" height="734" alt="Image" src="https://github.com/user-attachments/assets/87da224d-876f-4074-be46-1e81fb8f4992" />

<br />

<table>

  <tr>
    <td><img src="https://github.com/user-attachments/assets/63a2a38e-f114-4966-a698-00d70f9deaa3" width="400"/></td>
    <td><img src="https://github.com/user-attachments/assets/42612c60-dbe2-43e6-92d9-de6550330229" width="400"/></td>
  </tr>
  <tr>
    <td><img src="https://github.com/user-attachments/assets/b44261cf-effd-4760-af52-4ae058cb9d2e" width="400"/></td>
    <td><img src="https://github.com/user-attachments/assets/cece57c8-f317-43f9-b936-6f8d125b8e0e" width="400"/></td>
  </tr>
  <tr>
    <td><img src="https://github.com/user-attachments/assets/e0b86f35-4b77-47d0-b7ab-ee664a323843" width="400"/></td>
    <td><img src="https://github.com/user-attachments/assets/50713410-597f-4327-a798-8652360f9eb4" width="400"/></td>
  </tr>
</table>

<br />

## 1. 개요

풀, 꽃, 양치, 덩굴, 켈프, 수련처럼 블록(1×1×1)과 **다른 메쉬를 가지는 세밀한 오브젝트**를 Instance로 구분하였다.

청크 로드 시 지형과 트리가 놓인 후 Instance를 배치하고 이 Instance는 ChunkManager에서 정보를 모아 Cross / Fence / Square / Floor 4가지 quad 프리미티브 중 하나로 그려진다.

<br />

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

### 2.2 InstanceTypeInfo 내용

```cpp
class InstanceTypeInfo {
    ...
private:
	INSTANCE_SHAPE m_shape;          // 모양 종류 4 타입 중 하나
	TEXTURE_INDEX m_texIndex;        // 기본 텍스쳐 인덱스
	TEXTURE_INDEX m_texTopIndex;     // 위아래 구분된 Instance에서 윗방향 텍스쳐 인덱스
	TEXTURE_INDEX m_texBottomIndex;  // 위아래 구분된 Instance에서 아랫방향 텍스쳐 인덱스
	uint8_t m_maxHeight;             // 인스턴스의 최대 높이
};
```

### 2.3 4개 shape 종류의 성격을 가지는 Instance

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

### 2.4 등록된 인스턴스 타입 목록 일부

| INSTANCE_TYPE          | SHAPE  | texIndex           | texTopIndex           | texBottomIndex           | maxHeight |
| ---------------------- | :----: | ------------------ | --------------------- | ------------------------ | :-------: |
| `INSTANCE_GRASS`       | CROSS  | `SHORT_GRASS`      | `DOUBLE_GRASS_TOP`    | `DOUBLE_GRASS_BOTTOM`    |     2     |
| `INSTANCE_FERN`        | CROSS  | `FERN`             | `DOUBLE_FERN_TOP`     | `DOUBLE_FERN_BOTTOM`     |     3     |
| `INSTANCE_KELP`        | CROSS  | `KELP_TOP`         | `KELP_TOP`            | `KELP`                   |    10     |
| `INSTANCE_SEAGRASS`    | CROSS  | `SEAGRASS`         | `DOUBLE_SEAGRASS_TOP` | `DOUBLE_SEAGRASS_BOTTOM` |     2     |
| `INSTANCE_OXEYE_DAISY` | CROSS  | `OXEYE_DAISY`      | `NONE`                | `NONE`                   |     1     |
| `INSTANCE_TULIP_RED`   | CROSS  | `TULIP_RED`        | `NONE`                | `NONE`                   |     1     |
| `INSTANCE_SWEET_BERRY` | FENCE  | `SWEET_BERRY_BUSH` | `NONE`                | `NONE`                   |     1     |
| `INSTANCE_ROSE_PLANTS` | FENCE  | `ROSE_PLANTS`      | `NONE`                | `NONE`                   |     1     |
| `INSTANCE_VINE`        | SQUARE | `VINE`             | `NONE`                | `NONE`                   |     1     |
| `INSTANCE_WATER_LILY`  | FLOOR  | `WATER_LILY`       | `NONE`                | `NONE`                   |     1     |

<br />

## 3. Instance 심기 파이프라인 — `Chunk::InitInstancePlace`

`Chunk::Initialize` 의 5번째 단계 (Tree 다음).

```
InitTerrainNoises → InitBiomeMapAndCount → InitBasicBlockType → InitTreePlace
                                                                       ↓
                                                              InitInstancePlace   ← 이 문서
                                                                       ↓
                                                              InitWorldVerticesData
```

전체 흐름: Tree와 유사하다.

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

        // (2-a) 물 위 인스턴스 (수련) 먼저 시도 — y = 64 청크에서만
        if (CanPlaceWaterPlaneInstanceAt(x, z, memory)) {
            INSTANCE_TYPE instanceType = GetWaterPlaneInstanceType(x, z, memory);
            SetWaterPlaneInstance(x, z, instanceType, memory);
        }

        // (2-b) 땅 위 인스턴스 시도 (풀/꽃/양치/덩굴...)
        BIOME_TYPE biomeType = memory->biomeMap2D[x + 1][z + 1];
        float elevationWorldY = std::ceil(memory->elevationNoises[x + 1][z + 1]);
        int   localY = (int)(elevationWorldY - m_offsetPosition.y);

        if (CanPlaceBiomeInstanceAt(x, localY, z, placedBiomeInstanceCount[biomeType], memory)) {

            // (3) Biome에 따른 Instance Type 결정
            INSTANCE_TYPE instanceType = GetBiomeInstanceType(x, localY, z, memory);

            // (4) Instance 심기
            SetBiomeInstance(x, localY, z, instanceType, memory);

            placedBiomeInstanceCount[biomeType]++;
        }
    }
}
```

### 3.1 랜덤 위치 선정

Tree 와 완전히 동일한 `Terrain::GenerateRandomPlace2D` 를 사용하지만 SOLT 값이 달라 다른 값을 사용한다.

`INSTANCE_PLACE_MAX_COUNT_PER_CHUNK = 256` 개의 인스턴스를 심을 기준 후보를 xz 2D 격자 내에서 뽑는다.

`INSTANCE_PLACE_MAX_COUNT_PER_CHUNK = 256`는 상한이며, 실제 배치되는 개수는 biome-별 비율로 따로 계산한다.

```cpp
// voxen/headers/Terrain.h
static void GenerateRandomPlace2D(Vector3 worldPosition, uint32_t soltX, uint32_t soltZ,
    int maxPlaceCount, int indexCount,
    std::vector<std::pair<int, int>>& outRandomPlace2D)
{
    uint32_t seedX = (uint32_t)floor(worldPosition.x) + soltX;
    uint32_t seedZ = (uint32_t)floor(worldPosition.z) + soltZ;
    uint32_t soltY = (uint32_t)floor(worldPosition.y);

    for (int i = 0; i < maxPlaceCount; ++i) {
        seedX = Utils::HashInt(seedX, soltX * soltY);
        seedZ = Utils::HashInt(seedZ, soltZ * soltY);
        outRandomPlace2D.push_back({ seedX % indexCount, seedZ % indexCount });
    }
}
```

### 3.2 인스턴스 가능 검사 — 두 종류

#### (a) `CanPlaceWaterPlaneInstanceAt` — 수련 조건

```cpp
bool Chunk::CanPlaceWaterPlaneInstanceAt(int x, int z, ChunkLoadMemory* memory)
{
    const float WATER_HEIGHT = 64.0f;

    if (m_offsetPosition.y != WATER_HEIGHT) return false;                  // [A] y=64 청크만
    if (elevationWorldY   >= WATER_HEIGHT)  return false;                  // [B] 지형이 수면 위면 X
    if (GetWaterPlaneInstanceType(x,z,mem) == INSTANCE_NONE) return false; // [C] t/h/d 조건 불충족으로 만족하는 인스턴스가 없음
    return true;
}

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
    // [A] biome 비율에 대한 최대 개수 검사
    BIOME_TYPE biomeType = memory->biomeMap2D[x + 1][z + 1];
    uint32_t maxInstanceCountByRatio = GetMaxPlaceCountByBiomeRatio(
        Biome::GetMaxInstanceCountPerChunk(biomeType), memory->biomeCount[biomeType]);
    if (placedBiomeInstanceCount >= maxInstanceCountByRatio) return false;

    // [B] 청크 내부여야 함
    if (!IsInsideChunk(x, y, z))                                 return false;

    // [C] 같은 위치에 다른 인스턴스가 이미 있으면 X (수련 vs 지상 겹침 방지)
    if (IsInstanceAt(x, y, z))                                   return false;

    // [D] 이 biome 이 이 좌표에 어떤 인스턴스를 갖는지 결정 후 unset 이면 X
    INSTANCE_TYPE type = GetBiomeInstanceType(x, y, z, memory);
    if (type == INSTANCE_TYPE::INSTANCE_NONE)                    return false;

    // [E] shape / block 결합 조건 (Instance::CanPlace를 호출)
    return CheckInstancePlaceCondition(type, x, y, z);
}

bool Chunk::CheckInstancePlaceCondition(INSTANCE_TYPE type, int x, int y, int z)
{
	BLOCK_TYPE currentBlockType = m_blocks[x + 1][y + 1][z + 1].GetType();
	BLOCK_TYPE bottomBlockType = m_blocks[x + 1][y][z + 1].GetType();

	return Instance::CanPlace(type, currentBlockType, bottomBlockType); // 결합 조건 판단 (ex. 아래가 잔디 위에는 공기인가)
}
```

### 3.3 인스턴스 종류 결정 — `Instance::GetInstanceTypeForBiome`

Tree 의 `GetTreeTypeForBiome` 와 완전히 같은 패턴이다.

`Instance::GetInstanceTypeForBiome`은 그 후보 목록에서 `d` (distribution 노이즈) 값에 따라 하나를 선택하는 switch/case 다.

- cf. OOP로 구성하지 않은 점에 대한 단점이 보인다. Biome-Tree가 커플링 되어있다.

```cpp
// voxen/srcs/Instance.cpp
INSTANCE_TYPE Instance::GetInstanceTypeForBiome(BIOME_TYPE biomeType, float d, PosInt3 worldPos)
{
    const std::vector<INSTANCE_TYPE>& biomeInstances = Biome::GetInstances(biomeType);
    int worldY = std::get<1>(worldPos);

    switch (biomeType) {
    case BIOME_OCEAN:
        return (worldY < 52 && d < 0.5f) ? biomeInstances[0] : biomeInstances[1];

    case BIOME_PLAINS:
        if      (d < 0.70f) return biomeInstances[0];                                        // grass
        else if (d < 0.83f) return biomeInstances[Utils::RandomRangeByPos(worldPos, 1, 2)];  // daisy/cornflower
        else                return biomeInstances[Utils::RandomRangeByPos(worldPos, 3, 6)];  // 4종 tulip
    ...
    }
}
```

### 3.4 인스턴스 심기 — `SetBiomeInstance` / `SetWaterPlaneInstance`

각 인스턴스는 배치 시 좌표 hash 로 **yaw 회전**과 **xz 이동 오프셋**을 뽑아 **같은 종이라도 미묘하게 다르게** 보이도록 한다.

Instance에는 (type, texIndex, rotation, offset)이 들어가게 되고, `ChunkManager`가 모아서 처리하게 된다.

주변 청크에 심어야하는 경우, Patch를 이용한다.

- Tree는 GreedyMeshing으로 인한 패치 전파, 주변 청크 넘는 상황이 여러가지라 고려할 게 많았지만, Instance는 단순히 윗방향에 대해서 Patch를 기록한다.
- `loadPatchResult` 는 청크 로드 완료 후 `ChunkManager` 가 모아서 [Patch 파이프라인](../../ChunkManagement/ChunkPatch/README.md) 으로 넘긴다.

```cpp
void Chunk::SetBiomeInstance(int x, int y, int z, INSTANCE_TYPE type, ...)
{
    PosInt3 worldPosInt3 = ...;
    float rangeRotation  = Utils::RandomRangeByPos(worldPosInt3, 0, 360);              // 회전
    float ofsX = (Utils::RandomRangeByPos(worldPosInt3, 0, 50) - 25) / 100.0f;         // XZ 오프셋 -0.25 ~ +0.25
    float ofsZ = (Utils::RandomRangeByPos(worldPosInt3, 0, 50) - 25) / 100.0f;
    Vector2 rangeOffsetNoiseXZ = { ofsX, ofsZ };

    // 다층 인스턴스의 높이 처리 (kelp, grass, fern...)
    int instanceMaxH = Instance::GetMaxHeight(type);
    float thPercent  = (temperature + humidity) * 0.5f;
    int thMaxH       = (int)std::ceil(thPercent * instanceMaxH);         // 온도, 습도에 영향을 받게 된다.
    int rangeHeight  = Utils::RandomRangeByPos(worldPosInt3, 1, thMaxH); // 그것을 랜덤 Range로 최종 높이를 가져옴

    // 인스턴스 실제로 대입하기
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

<br />

## 4. Instanced Rendering

블록과 달리 인스턴스는 **한 셀에 quad 몇 개** 밖에 되지 않고 위치·회전·텍스처만 조금씩 다른 같은 mesh 가 청크에 수백~수천 개 뿌려진다.

이 특성에 맞게 D3D11 의 `DrawIndexedInstanced` 를 사용해 종류 당 한 번의 DrawCall로 모두 렌더링하여 DrawCall을 줄인다.

### 4.1 두 개의 vertex buffer — Per-Vertex + Per-Instance

인스턴스 렌더링은 두 가지 데이터를 각각 다른 IA (Input Assembler)로 사용한다.

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
  매 프레임 render chunk list 순회하며 ChunkManager가 채우고 초기화한다.

### 4.2 앱 초기화 시 Per-Vertex는 미리 초기화 — `MakeInstanceVertexBuffer` (base mesh 4 종)

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

### 4.3 매 프레임 — `UpdateInstanceInfoList` (ChunkManager 가 한 번에 수집)

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
        if (c->IsUpdateRequired()) continue;                                 // 업데이트하고 있는 청크는 skip
        if (chunkCenterDistance > Camera::LOD_RENDER_DISTANCE) continue;     // 원거리 LOD 청크는 인스턴스 안 그림

        // (3) 각 청크의 m_instanceMap 을 shape 별 buffer 에 뿌림
        for (auto& [localPos, instance] : c->GetInstanceMap()) {
            Vector3 worldPosition = c->GetOffsetPosition() + PosInt3ToVector(localPos);

            if (instance.GetFaceFlag() > 0)                                  // vine 은 방향별 quad split
                AddInstanceInfoBySplitFace(worldPosition, instance);
            else
                AddInstanceInfo(worldPosition, instance);                    // 일반: m_instanceInfoList에 push
        }
    }

    // (4) shape 4개 각각의 GPU 버퍼로 한 번씩 upload
    for (int i = 0; i < INSTANCE_SHAPE_COUNT; ++i) {
        DXUtils::ResizeBuffer(m_instanceInfoBuffers[i], m_instanceInfoList[i],
                              D3D11_BIND_VERTEX_BUFFER, m_instanceInfoList[i].size() + 1024); // 필요 시 진행됨
        DXUtils::UpdateBuffer (m_instanceInfoBuffers[i], m_instanceInfoList[i]);
    }
}
```

핵심 포인트:

- 인스턴스 데이터는 **청크 각자가 아니라 `ChunkManager` 가 하나의 큰 배열로 모아서 관리**한다.
- 시야 밖 / 로딩 중 / 원거리 청크는 여기서 필터링되므로 GPU 로 올라가는 데이터가 그만큼 줄어든다.
- `AddInstanceInfo` 는 `instance` 하나로부터 `InstanceInfoVertex` 하나를 만들어 (world = translate·yaw·offset, texIndex 그대로) 해당 shape 배열에 push.
- Vine 은 방향 flag (`V_LEFT / V_RIGHT / V_FRONT / V_BACK`) 마다 벽 면이 다르므로 `AddInstanceInfoBySplitFace` 로 방향별로 회전 시켜 `InstanceInfoVertex` 를 만들어 넣는다.

### 4.4 AddInstanceInfo — 실질적 GPU 데이터 구성

Per-Instance 데이터는 WorldMatrix와 TexIndex만을 가지고 있다.

Instance에 저장된 데이터를 가지고 구성한다.

```cpp
ChunkManager::AddInstanceInfo(Vector3 worldPosition, const Instance& instance) {
    InstanceInfoVertex info;

    INSTANCE_TYPE type = instance.GetType();

    // 텍스쳐 인덱스 구성
    info.texIndex = instance.GetTexIndex();

    // 이동Noise를 더해 월드 위치 Matrix 구성
    float offsetNoiseX = instance.GetOffsetNoisePositionXZ().x;
    float offsetNoiseZ = instance.GetOffsetNoisePositionXZ().y;
    Vector3 offsetNoiseXZ = Vector3(0.5f) + Vector3(offsetNoiseX, 0.0f, offsetNoiseZ);
    Vector3 instanceWorldPosition = worldPosition + offsetNoiseXZ;
    Matrix translation = Matrix::CreateTranslation(instanceWorldPosition);

    // 회전Noise를 더해 회전 Matrix 구성
    float yawRotationRadian = instance.GetYawRotation() * (XM_PI / 180.0f);
    Matrix rotation = Matrix::CreateFromQuaternion(
        Quaternion::CreateFromAxisAngle(Vector3(0.0f, 1.0f, 0.0f), yawRotationRadian));

    info.instanceWorld = (rotation * translation).Transpose();

    INSTANCE_SHAPE shapeType = instance.GetShape(type);
    m_instanceInfoList[shapeType].push_back(info);
}
```

### 4.5 Draw — `RenderInstance` (shape 4 개 × 각 1 draw call)

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
        context->DrawIndexedInstanced(indexCountPerInstance[i], m_instanceInfoList[i].size(), 0, 0, 0);
    }
}
```

- 프레임당 총 draw call 은 **shape 개수 만큼 (= 4 회)** 이 된다.
  청크 수나 실제 인스턴스 개수와 무관.
- 셰이더에서는 IA slot 0 에서 정점 데이터 (`InstanceVertex.position, normal, texcoord`) 를,
  slot 1 에서 인스턴스 데이터 (`instanceWorld`, `texIndex`)를 받아 위치를 변환하고 지정된 TexIndex를 렌더링하게 된다.

<br />

## 5. 회고

블록 타입과 다른 형태의 메쉬를 렌더링하느라 Instance Rendering을 사용하게 되었고 그에 따라 다양한 수정이 필요했다.

- GreedyMeshing으로 1x1x1은 모두 통일하게 되었지만, 작은 블록의 형태나 Sprite 형태의 메쉬를 어떻게 렌더링할지 고민이 많았다.
- 그래서, Chunk 내부에 Block에서 Instance를 따로 뽑아 관리하게 되었다.
- 또한 Instance Rendering을 하게 되면서 Chunk 내부에 Instance GPU 버퍼는 ChunkManager가 관리하게 되었고, 그에 따라 다른 gpu 데이터도 ChunkManager가 관리하게 끔 구성하여 통일성을 구성하게 되었다.
  - Chunk (cpu 데이터) <-> ChunkManager(gpu 데이터)

아쉬운 점

- Block과 Instance를 둘로 나뉘었을 때 단점이 명확하다
  - 공통된 특징은 많은데, 구분되어 있어 두가지를 따로 검사해야 한다.
  - DDA Picking을 하거나, 사용자가 직접 블록을 수정하는 경우 Block과 Instance를 모두 고려하는 상황이 발생했다.
  - 더구나, Patch 로직에서도 결국 Block, Instance 모두 따로 보는 로직이 구성되어 불편하기도 했다.
  - 물론 렌더링 시에는 구분이 되어야 할 것 같다.
    - ex. 백페이스 컬링

Per-Vertex Buffer ?

- Instance Rendering에서 Per-Vertex Buffer는 언제 사용하는지 학습이 되지 않았다.
  - 내가 구성한 4개의 Shape을 따로 Per-Vertex로 구성하고 사용하는 것인가?
  - 아니면 Per-Vertex x Per-Instance로 렌더링 되는 것인가? 아래가 맞다고 한다.

    ```
    for instance in 0..InstanceCount:
        for vertex in 0..VertexCountPerInstance:
            VS 실행(vertex의 Per-Vertex 데이터, instance의 Per-Instance 데이터)
    ```
