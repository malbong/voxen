# Tree System

지표가 결정된 후, 각 청크의 표면 좌표에 8종의 procedural tree (OAK / SPRUCE /
MANGROVE / BIRCH / CHERRY / CACTUS / JUNGLE / ACACIA) 를 심는 단계.

트리는 **25³ voxel scratch buffer(`TreeShape`) 위에 형태를 조립**한 뒤 이 결과를
청크의 `m_blocks` 배열에 stamp 한다. 최대 크기가 25 블록이라 청크(32 블록) 경계를
넘어 인접 청크에까지 걸치는 경우가 흔하고, 이때는 [Patch 시스템](../../ChunkManagement/README.md)
을 통해 인접 청크에 패치 데이터가 전파된다.

## 1. 개요

### 1.1 자료구조 — Tree → TreeTypeInfoSet → TreeTypeInfo

[Biome](../Biome/README.md), [Block](../Block/README.md) 과 완전히 같은
"정적 테이블 + 얇은 값 객체" 패턴이다.

- `Tree` — **정적 함수만 노출하는 접근 계층 (facade)**.
  개별 인스턴스는 `TREE_TYPE` 한 값만 갖는다.
- `TreeTypeInfoSet` — 256 (`TREE_TYPE_COUNT`) 크기 vector 로 `TreeTypeInfo` 를
  보관. 생성자에서 8개 트리 타입의 속성을 초기화.
- `TreeTypeInfo` — 한 트리 타입의 속성 3개:
  - `m_trunkBlockType` — 기둥 블록 (`BLOCK_OAK_LOG`, `BLOCK_CACTUS`, …)
  - `m_leafBlockType` — 잎 블록 (`BLOCK_OAK_LEAF`, …). CACTUS 는 trunk 와 동일하게 `BLOCK_CACTUS`.
  - `m_shapeParams` — `TreeShapeParams`. 아래 5개 정수.

```cpp
// voxen/headers/Tree.h
struct TreeShapeParams {
    int baseHeight;         // 기둥의 기본 높이 (변주는 +랜덤)
    int branchCount;        // 나뭇가지 개수
    int branchLength;       // 나뭇가지 하나당 길이
    int branchStartHeight;  // 가지가 나오는 y 시작점
    int leafRadius;         // 잎 클러스터 반지름
};

class Tree {
public:
    static const uint32_t TREE_TYPE_COUNT = 256;
    static const uint8_t  TREE_SIZE       = 25;   // scratch buffer 한 변 크기

    static BLOCK_TYPE              GetTrunkBlockType    (TREE_TYPE);
    static BLOCK_TYPE              GetLeafBlockType     (TREE_TYPE);
    static const TreeShapeParams&  GetTreeShapeParams   (TREE_TYPE);
    static TREE_TYPE               GetTreeTypeForBiome(BIOME_TYPE, float d,
                                                       int lx, int ly, int lz);
    static void                    GenerateTreeShape(TREE_TYPE, const PosInt3& worldPos,
                                                     TreeShape& outTree);
    ...
private:
    static TreeTypeInfoSet m_treeTypeInfoSet;    // 전역 정적 테이블
    TREE_TYPE              m_type;               // 인스턴스가 가진 유일한 상태
};
```

`Tree::GetTrunkBlockType(type)` 등은 아래처럼 정적 테이블로 위임된다.

```cpp
// voxen/srcs/Tree.cpp
TreeTypeInfoSet Tree::m_treeTypeInfoSet;

BLOCK_TYPE Tree::GetTrunkBlockType(TREE_TYPE type)
{
    return m_treeTypeInfoSet.GetInfo(type).GetTrunkBlockType();
}
```

`TreeTypeInfoSet` 생성자에 등록된 8개 타입의 `TreeShapeParams`:

| tree     | baseHeight | branchCount | branchLength | branchStartH | leafRadius |
| -------- | :--------: | :---------: | :----------: | :----------: | :--------: |
| OAK      |     7      |      0      |      0       |      0       |     3      |
| SPRUCE   |     12     |      0      |      0       |      0       |     4      |
| MANGROVE |     7      |      0      |      0       |      0       |     3      |
| BIRCH    |     6      |      0      |      0       |      0       |     2      |
| CHERRY   |     9      |      2      |      13      |      3       |     6      |
| CACTUS   |     7      |      2      |      4       |      4       |     0      |
| JUNGLE   |     19     |      3      |      13      |     13       |     7      |
| ACACIA   |     10     |      1      |      10      |      3       |     6      |

`branchCount=0` 인 OAK / SPRUCE / MANGROVE / BIRCH 는 **기둥 하나 + 그 위의 잎
클러스터** 만 있는 단순 형태이고, `branchCount>0` 인 CHERRY / CACTUS / JUNGLE / ACACIA
는 가지를 여러 번 뻗어 나가는 복잡한 형태다.

### 1.2 왜 OOP 를 쓰지 않았는가

앞선 Biome / Block 문서와 동일한 이유다.

- Tree 마다 다른 것은 **파라미터(트렁크/잎 블록, shape params)** 와 **모양 생성 함수** 뿐이다.
- 모양 생성은 `GenerateBasicTree` / `GenerateSpruce` / `GenerateJungle` / … 자유 함수들로
  분리되고, `Tree::GenerateTreeShape` 가 switch/case 로 dispatch 한다. 다형성이 필요 없고
  virtual 호출 오버헤드도 없다.
- 여러 청크에서 병렬로 트리를 심으므로 인스턴스 상태를 최소화(`m_type` 하나) 하는 것이 유리.

## 2. Tree 심기 파이프라인 — `Chunk::InitTreePlace`

`Chunk::Initialize` 의 4번째 단계다.

```
InitTerrainNoises → InitBiomeMapAndCount → InitBasicBlockType
                                                    ↓
                                           InitTreePlace         ← 이 문서
                                                    ↓
                                           InitInstancePlace
                                                    ↓
                                           InitWorldVerticesData
```

전체 흐름:

```cpp
// voxen/srcs/Chunk.cpp
void Chunk::InitTreePlace(ChunkLoadMemory* memory)
{
    // (1) 랜덤 좌표 후보 뽑기
    Terrain::GenerateRandomPlace2D(m_offsetPosition,
        TREE_PLACE_RANDOM_SOLT_X, TREE_PLACE_RANDOM_SOLT_Z,
        TREE_PLACE_MAX_COUNT_PER_CHUNK, CHUNK_SIZE,
        memory->treeRandomPlace2D);

    uint32_t placedBiomeTreeCount[BIOME_TYPE::BIOME_COUNT] = { 0, };

    for (int i = 0; i < TREE_PLACE_MAX_COUNT_PER_CHUNK; ++i) {
        int x = memory->treeRandomPlace2D[i].first;
        int z = memory->treeRandomPlace2D[i].second;

        BIOME_TYPE biomeType = memory->biomeMap2D[x + 1][z + 1];

        float elevationWorldY = std::floor(memory->elevationNoises[x + 1][z + 1]);
        int   localY = (int)(elevationWorldY - m_offsetPosition.y);

        // (2) 심을 수 있는지 검사
        if (CanPlaceTreeAt(x, localY, z, placedBiomeTreeCount[biomeType], memory)) {

            // (3) 트리 종류 결정
            TREE_TYPE treeType = Tree::GetTreeTypeForBiome(
                biomeType, memory->distributionNoises[x + 1][z + 1], x, localY, z);

            // (4) 심기 (shape 생성 + block/vine 배치 + 청크 경계 patch)
            PlaceTree(x, localY, z, memory, treeType);

            placedBiomeTreeCount[biomeType]++;
        }
    }
}
```

### 2.1 랜덤 위치 선정 — `Terrain::GenerateRandomPlace2D`

청크당 최대 `TREE_PLACE_MAX_COUNT_PER_CHUNK` (= `CHUNK_SIZE^2 / 16` = 64) 개의
xz 후보 좌표를 좌표 hash 기반으로 결정론적으로 뽑는다.

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

- 청크의 offset 좌표에서 시드를 얻어 시작하므로 **같은 청크는 항상 같은 후보 좌표 시퀀스**를 얻는다.
  → 청크가 unload 후 다시 load 되어도 나무 배치가 동일하게 재현된다.
- 좌표 시퀀스는 이후 순차적으로 심어보므로, 실패한 후보(quota 초과, 지형 조건 불만족)는 그냥 다음 후보로 넘어간다.
- `TREE_PLACE_MAX_COUNT_PER_CHUNK = 64` 는 상한이며, 실제 배치되는 개수는 biome-별 quota 로 제한된다.

### 2.2 위치 가능성 검사 — `CanPlaceTreeAt` + `CheckTreePlaceCondition`

세 단계의 필터가 걸린다.

```cpp
bool Chunk::CanPlaceTreeAt(int x, int y, int z, uint32_t placedBiomeTreeCount, ...)
{
    // [A] biome 별 quota
    BIOME_TYPE biomeType = memory->biomeMap2D[x + 1][z + 1];
    uint32_t maxTreeCountByRatio = GetMaxPlaceCountByBiomeRatio(
        Biome::GetMaxTreeCountPerChunk(biomeType), memory->biomeCount[biomeType]);
    if (placedBiomeTreeCount >= maxTreeCountByRatio) return false;

    // [B] 트리 밑동이 청크 안에 있어야 함
    if (!IsInsideChunk(x, y, z))                     return false;

    // [C] 이 biome 이 트리 타입을 갖는지
    TREE_TYPE treeType = Tree::GetTreeTypeForBiome(biomeType,
                            memory->distributionNoises[x + 1][z + 1], x, y, z);
    if (treeType == TREE_TYPE::TREE_NONE)            return false;

    // [D] 3x3 표면 조건 (밑 = opaque, 위 = air)
    for (int i = -1; i <= 1; ++i) {
        if (!CheckTreePlaceCondition(x + i, y, z))   return false;
        if (!CheckTreePlaceCondition(x, y, z + i))   return false;
    }
    return true;
}

bool Chunk::CheckTreePlaceCondition(int x, int y, int z)
{
    BLOCK_TYPE cur = m_blocks[x + 1][y + 1][z + 1].GetType();
    BLOCK_TYPE top = m_blocks[x + 1][y + 2][z + 1].GetType();
    if (!Block::IsOpaque(cur))         return false;   // 밑에 단단한 지면
    if (top != BLOCK_TYPE::BLOCK_AIR)  return false;   // 위는 공기
    return true;
}
```

- **[A] quota** — biome 이 청크를 얼마나 차지하는지에 비례해서 최대 트리 수를 정한다. FOREST 는 청크 전체가 forest 면 최대 32그루, half 만 forest 면 16그루.
- **[B] 밑동은 반드시 청크 내부** — 트리는 인접 청크로 뻗어 나갈 수 있지만 시작점(밑동) 은 이 청크가 소유한다. 이 규칙으로 두 청크가 같은 트리를 이중으로 심는 상황이 발생하지 않는다.
- **[C] TREE_NONE 처리** — `GetTreeTypeForBiome` 가 OCEAN 처럼 트리 없는 biome 이면 `TREE_NONE` 을 반환하지 않지만 (첫 원소 그대로 반환) TUNDRA 첫 원소가 `TREE_SPRUCE_LOG` 인 것처럼 항상 유효한 타입이 나오도록 데이터에서 보장된다.
  안전장치로 여기서도 한 번 더 체크.
- **[D] 3x3 밑동 조건** — 밑동 좌표뿐 아니라 x/z ±1 (십자 5칸) 이 모두 opaque 지면 + 위 공기여야 한다. 절벽 가장자리에 트리가 심어져 공중에 떠 있는 상황을 방지.

### 2.3 트리 종류 결정 — `Tree::GetTreeTypeForBiome`

biome 마다 후보 트리가 이미 [BiomeTypeInfoSet](../Biome/README.md#12-왜-oop-를-쓰지-않았는가) 에
등록돼 있다. `Tree::GetTreeTypeForBiome` 는 그 후보 목록에서 `d` (distribution 노이즈) 값에 따라
하나를 선택하는 얇은 switch/case 다.

```cpp
// voxen/srcs/Tree.cpp
TREE_TYPE Tree::GetTreeTypeForBiome(BIOME_TYPE biomeType, float d, ...)
{
    const std::vector<TREE_TYPE>& biomeTrees = Biome::GetTrees(biomeType);

    switch (biomeType) {
    case BIOME_OCEAN:    return biomeTrees[0];                            // none
    case BIOME_PLAINS:   return biomeTrees[0];                            // oak
    case BIOME_TUNDRA:
    case BIOME_TAIGA:    return biomeTrees[0];                            // spruce

    case BIOME_SWAMP:    return d < 0.3f ? biomeTrees[0] : biomeTrees[1]; // oak / mangrove
    case BIOME_FOREST:   return d < 0.8f ? biomeTrees[0] : biomeTrees[1]; // oak / birch
    case BIOME_SAVANNA:  return d < 0.3f ? biomeTrees[0] : biomeTrees[1]; // oak / acacia
    ...
    }
}
```

- 후보가 1종인 biome (`TUNDRA`, `PLAINS`, `DESERT`, `TAIGA`, `SNOWY_TAIGA`) 은 항상 그 하나만.
- 후보가 2종인 biome 은 `d` 값의 threshold 로 확률 결정. `d < 0.3` 이면 30% 확률로 첫 후보.
- distribution 노이즈는 scale 24 로 픽셀 단위 랜덤이라, 근접한 트리들끼리도 다른 종류가 섞여 자연스럽다.

### 2.4 트리 심기 — `PlaceTree`

밑동 위치 (`x, y, z`) 가 정해지면, 25³ scratch buffer 에 형태를 조립하고 청크의 `m_blocks` 로 stamp 한다.

```cpp
void Chunk::PlaceTree(int x, int y, int z, ChunkLoadMemory* memory, TREE_TYPE treeType)
{
    PosInt3 worldPosInt3 = ...;
    TreeShape treeShape = { TREE_BLOCK_INDEX::EMPTY };
    Tree::GenerateTreeShape(treeType, worldPosInt3, treeShape);      // [I] 모양 생성

    for (int dy = 0; dy < Tree::TREE_SIZE; ++dy)
    for (int dz = 0; dz < Tree::TREE_SIZE; ++dz)
    for (int dx = 0; dx < Tree::TREE_SIZE; ++dx) {
        if (treeShape[dy][dz][dx] == TREE_BLOCK_INDEX::EMPTY) continue;

        // scratch buffer 좌표 → 청크 로컬 좌표
        int ty = y  +  dy;
        int tz = z  -  dz + (Tree::TREE_SIZE / 2);
        int tx = x  +  dx - (Tree::TREE_SIZE / 2);

        // [II] trunk / leaf → 블록으로 stamp
        if (treeShape[dy][dz][dx] == TRUNK || treeShape[dy][dz][dx] == LEAF) {
            BLOCK_TYPE treeBlock = (treeShape[dy][dz][dx] == TRUNK)
                ? Tree::GetTrunkBlockType(treeType)
                : Tree::GetLeafBlockType(treeType);
            SetTreeBlockType(tx, ty, tz, treeBlock, memory);
        }
        // [III] vine 은 instance 로 심음 (블록 아님)
        if ((treeShape[dy][dz][dx] & VINE) == VINE) {
            uint8_t faceFlag = (treeShape[dy][dz][dx] & (~VINE));   // 상위 비트가 방향
            SetTreeVines(tx, ty, tz, INSTANCE_VINE, faceFlag, memory);
        }
    }
}
```

세 단계 [I] → [III] 을 아래에서 상세히 본다.

## 3. 모양 결정 알고리즘

### 3.1 `TreeShape` — 25³ 스크래치 버퍼

```cpp
// voxen/headers/Tree.h
using TreeShape = uint8_t[25][25][25];   // [y][z][x]
```

각 셀은 아래 4개 값 중 하나 + vine 방향 비트 조합.

```cpp
// voxen/headers/Enums.h
enum TREE_BLOCK_INDEX : uint8_t {
    EMPTY = 0, TRUNK = 1, LEAF = 2, VINE = 3,
};
enum VINE_DIR : uint8_t {
    V_LEFT  = (1 << 2),   // 트리 좌측면에서 오른쪽으로 늘어짐
    V_RIGHT = (1 << 3),
    V_FRONT = (1 << 4),
    V_BACK  = (1 << 5),
};
```

- 하위 2 비트는 `TREE_BLOCK_INDEX` (2 비트로 4가지). VINE 은 최상위 방향 비트와 함께 저장되므로 하나의 uint8_t 로 blocktype + vine 방향을 모두 담는다.
- Buffer 중심은 `(x, y, z) = (12, 0, 12)` — 밑동 위치. 위로 자라나는 나무는 y 방향으로 채워진다.
- Cactus 같은 예외를 제외하면 트리의 최대 y 는 baseHeight + 랜덤 범위 정도라 25 안에서 충분.

### 3.2 `Tree::GenerateTreeShape` — 5개 생성 함수 dispatch

```cpp
void Tree::GenerateTreeShape(TREE_TYPE type, const PosInt3& worldPosInt3, TreeShape& outTreeShape)
{
    const TreeShapeParams& params = GetTreeShapeParams(type);
    switch (type) {
    case TREE_MANGROVE_LOG:  return GenerateMangrove (params, worldPosInt3, outTreeShape);
    case TREE_CHERRY_LOG:    return GenerateCherry   (params, worldPosInt3, outTreeShape);
    case TREE_CACTUS:        return GenerateCactus   (params, worldPosInt3, outTreeShape);
    case TREE_JUNGLE_LOG:    return GenerateJungle   (params, worldPosInt3, outTreeShape);
    case TREE_ACACIA_LOG:    return GenerateAcacia   (params, worldPosInt3, outTreeShape);
    case TREE_SPRUCE_LOG:    return GenerateSpruce   (params, worldPosInt3, outTreeShape);
    default:                 return GenerateBasicTree(params, worldPosInt3, outTreeShape); // OAK, BIRCH
    }
}
```

여러 종이 공유하는 **빌딩 블록**이 4개 있다.

#### (a) 기둥 (Trunk)

가장 단순한 경우 (`GenerateBasicTree`): 중심 (12, 12) 에서 y 방향으로 baseHeight ± 랜덤 만큼 TRUNK 를 채운다.
JUNGLE 은 2x2 굵기의 기둥 4칸을 동시에 세운다.

```cpp
int heightRange = params.baseHeight + Utils::RandomRangeByPos(worldPos, -1, 1);
for (int y = 0; y < heightRange; ++y)
    tree[y][tz][tx] = TREE_BLOCK_INDEX::TRUNK;
```

#### (b) 나뭇가지 (Branch)

두 가지 진행 방식이 있다.

**Gradient branch** (`AddBranchForGradient`) — CHERRY / JUNGLE / ACACIA 에서 사용.
목표 gradient 방향으로 6방향 후보 중 "gradient 와 가장 잘 정렬되고 목표에서 멀어지는" 방향을 매 스텝 선택한다.
결과적으로 gradient 방향으로 부드럽게 뻗어나가는 곡선.

```cpp
Vector3 curDir = GenerateNextDirectionForGradient(diffPos, gradient);
```

**Random branch** (`AddBranchForRandom`) — CACTUS 에서 사용.
6방향 중 역방향/이전 역방향/초기 역방향을 제외한 랜덤 방향으로 진행. y 축 방향은 특정 loop 이후에만 허용해 처음엔 수평, 나중엔 위로 굽는 형태.

가지 하나의 마지막 위치(`lastBranchPos`) 는 잎 클러스터의 중심이 된다.

#### (c) 잎 클러스터 (Leaf Cluster / Plane)

**AddLeafCluster** — 3D 타원구를 채운다. `shrink` 로 축 비율을 조정 (SPRUCE 는 y 방향 2.5배 늘려 원뿔) 하고,
`carve` 구간에서는 좌표 hash 로 x/z 를 random 하게 0/1 만큼 늘려 표면을 오돌토돌하게 만든다.

```cpp
// 타원구 정의
if (sx*sx + sy*sy + sz*sz < radius*radius) {
    tree[ny][nz][nx] = TREE_BLOCK_INDEX::LEAF;
}
```

**AddLeafPlane** — 얇은 2D 원판 (y=center.y 한 층만). ACACIA / JUNGLE 처럼 우산 모양 잎이 필요할 때 사용.

#### (d) 덩굴 (Vine)

`AddVines` 는 완성된 shape 를 다시 스캔하면서 **LEAF/TRUNK 옆의 EMPTY 칸**에 대해
좌표 hash 기반 확률로 vine 을 생성한다.

```cpp
for y from top to bottom:
  for each (x, z) in TreeShape:
    if tree[y][z][x] != LEAF && tree[y][z][x] != TRUNK: continue

    for d in {L, R, F, B}:
        nx, nz = neighbor in direction d
        if tree[y][nz][nx] != EMPTY: continue
        if hash(world position) % 100 <= vinePercent:
            length = random(1, y)
            DropVines(nx, y, nz, d, length, tree)   // 아래로 length 칸 늘어뜨림
```

`DropVines` 는 (nx, y, nz) 부터 y 감소 방향으로 length 칸까지 진행하며 EMPTY 인 칸에
`VINE | V_LEFT/RIGHT/FRONT/BACK` 를 덧씌운다. `d` 는 어느 트리 면에서 나온 vine 인지를 기록해서
나중에 렌더링할 때 vine 이 벽에 붙는 방향을 결정한다.

- 방향-비트 인코딩 규칙: 트리에서 왼쪽으로 나온 vine 은 그 vine 을 vine 셀 기준으로 보면 오른쪽에 트리가 있으므로 `V_RIGHT` (트리가 붙은 방향) 로 저장.
- **JUNGLE** 은 `vinePercent = 5 + RandomRange(0, 5)` 로 드물게, **MANGROVE** 는 `40 + RandomRange(-10, 10)` 로 매우 촘촘하게 덩굴을 늘어뜨린다.

### 3.3 각 트리 타입의 조합

| 타입     | 함수                | trunk        | branch                   | leaf                             | vine       |
| -------- | ------------------- | ------------ | ------------------------ | -------------------------------- | ---------- |
| OAK, BIRCH | GenerateBasicTree | 1칸, ±1      | 없음                     | Cluster (타원구) + cap Cluster   | 없음       |
| SPRUCE   | GenerateSpruce      | 1칸, ±2/+4   | 없음                     | Cluster (y-2.5배 늘림) + cap     | 없음       |
| MANGROVE | GenerateMangrove    | 1칸, ±0/-1   | 없음                     | Cluster (y-1.5 배)               | 40% (많음) |
| CHERRY   | GenerateCherry      | 1칸, ±1      | Gradient × 2             | 각 가지 끝 Cluster + 본체 Cluster | 없음      |
| CACTUS   | GenerateCactus      | 1칸, ±1      | Random × 2 (수평 후 위)  | 없음                             | 없음       |
| JUNGLE   | GenerateJungle      | 2x2, ±2      | Gradient × 3             | 각 가지 끝 Plane + 본체 3층 Plane | 5%(적음)  |
| ACACIA   | GenerateAcacia      | 1칸, ±1      | Gradient × 1             | 가지 끝 Plane + 본체 2층 Plane   | 없음       |

## 4. 청크 경계 처리 — Patch 전파

트리 크기가 최대 25 블록인데 청크 하나는 32 블록이므로, 청크 가장자리(x < 13 또는 x >= 19 등)에 심어진 트리는 인접 청크로 뻗어나간다.
청크는 각자 독립적으로 로드되므로 다른 청크에 직접 값을 쓸 수 없고, 대신 **패치 데이터를 만들어 두면 이후 Patch 파이프라인이 인접 청크에 반영**한다.

### 4.1 `SetTreeBlockType` — 3-way 분기

```cpp
void Chunk::SetTreeBlockType(int tx, int ty, int tz, BLOCK_TYPE treeBlock, ...)
{
    // [1] padding 포함 자기 청크 안이면 즉시 stamp
    if (IsInsideChunkWithPadding(tx, ty, tz)) {           // -1 <= tx <= 32
        m_blocks[tx + 1][ty + 1][tz + 1].SetType(treeBlock);
    }

    // [2] 자기 청크 밖(padding 밖) 이면 이웃 청크로 patch 요청
    if (!IsInsideChunk(tx, ty, tz)) {
        PatchData patchData(tx, ty, tz, treeBlock, Instance(), CHUNK_SIZE, true);
        Vector3 blockOwnerOffset = Utils::CalcOffsetPos(m_offsetPosition + Vector3(tx,ty,tz), CHUNK_SIZE);
        memory->loadPatchResult[Utils::VectorToPosInt3(blockOwnerOffset)].insert(patchData);
    }

    // [3] 청크 안이더라도 (Inner/Outer) Edge 이면 그리디 메싱 seam 방지 위해 인접 청크에도 patch
    if (IsInnerEdge(tx, ty, tz) || IsOuterEdge(tx, ty, tz)) {
        PatchData::GenerateEdgePatchEntry(...);            // 최대 3개 인접 청크
        for each entry: memory->loadPatchResult[...].insert(patchData);
    }
}
```

- `IsInsideChunkWithPadding` 은 `-1 ≤ x ≤ 32` 로 padding 을 포함한 범위 (`CHUNK_SIZE_P` 배열의 유효 인덱스).
  이 안에 들어오면 padding cell 이라도 곧바로 값을 써 둔다. Greedy meshing 이 padding 값을 참조해서 face 를 컬링하기 때문.
- `!IsInsideChunk` 는 청크의 32×32×32 영역 밖. 이때는 자기 배열이 아니라
  **소유 청크(blockOwnerOffsetPos) 를 계산해서 그 청크의 patch queue 에 데이터를 넣는다.**
- Edge 블록은 인접 청크의 mesh 도 영향을 받으므로 (자기 청크 안에 있어도) patch 를 함께 보낸다.
  이 처리가 없으면 청크 경계에서 나뭇가지가 뭉그러진 것처럼 보이는 seam 이 생긴다.

### 4.2 Vine 은 instance 로 patch

```cpp
void Chunk::SetTreeVines(int tx, int ty, int tz, INSTANCE_TYPE treeVine, uint8_t faceFlag, ...)
{
    Instance instance = Instance(treeVine, texIndex, 0.0f, Vector2(0.0f), faceFlag);

    if (IsInsideChunk(tx, ty, tz)) {
        m_instanceMap.insert({ PosInt3(tx, ty, tz), instance });    // 자기 청크
    }
    else {
        PatchData patchData(tx, ty, tz, Block(), instance, CHUNK_SIZE, true);
        memory->loadPatchResult[ownerOffset].insert(patchData);     // 이웃 청크로 patch
    }
}
```

- Vine 은 블록이 아니라 **instance** (cross-billboard 형 메쉬) 로 렌더링되므로 별도의 `m_instanceMap` 에 들어간다.
- `faceFlag` 는 vine 이 붙은 트리 면의 방향으로, 렌더러가 이걸 보고 vine quad 를 어느 벽 쪽으로 붙일지 결정.
- 청크 밖 vine 도 마찬가지로 patch 데이터로 보낸다.

### 4.3 로드 시점의 패치 큐 → Patch 파이프라인

`memory->loadPatchResult` 는 `PosHashMap<PatchDataHashSet>` (청크 offset → 패치 세트) 이다.
`Chunk::Initialize` 가 리턴한 후 ChunkManager 가 이 결과를 모아서 [Patch 파이프라인](../../ChunkManagement/README.md) 으로 넘긴다.
자세한 처리(UpdatePatchChunkMap, PatchChunks, SyncPatchedChunks) 는 ChunkManagement 문서 참고.

## 5. 회고

- **25³ scratch buffer 방식** 이 트리 로직을 아주 단순하게 만들어 준다. Trunk / branch / leaf / vine 이 다 `TreeShape` 라는 로컬 좌표계에서만 조립되고, "청크와의 경계" 는 마지막에 stamp 단계에서만 신경 쓰면 된다.

- **함수형 구성 요소 조합** — `AddLeafCluster`, `AddLeafPlane`, `AddBranchForGradient`, `AddBranchForRandom`, `AddVines` 같은 자유 함수가 각 tree type 함수 안에서 조합되는 방식이라, 새 트리를 추가할 때 새 조합 하나만 만들면 된다. OOP 상속으로 짰다면 각 트리 클래스가 이 함수들을 override 하거나 helper 를 갖게 되어 훨씬 복잡했을 것이다.

- **아쉬운 점** : `GenerateJungle` / `GenerateAcacia` / `GenerateCherry` 가 서로 유사한 코드 구조(가지 for 문 → 잎 → 본체 잎) 를 갖는데 파라미터 몇 개만 다르다. `LeafPlane` 두 층 vs `LeafCluster` 처럼 잎 스타일까지 데이터로 뽑아내면 함수 하나로 통합 가능.

- **개선하고 싶은 방향** : 현재 branch 방향은 랜덤 gradient 이므로 트리의 실루엣이 좌우 대칭이 되지 않고, 청크 seed 만으로 결정되기 때문에 같은 좌표에서 항상 같은 형태가 나온다. gradient 를 world position + 시간 시드로 뽑거나, 몇 개 preset shape 을 미리 준비해 hash 로 선택하는 하이브리드 방식이 시각적 다양성 대비 비용이 낮을 것.
