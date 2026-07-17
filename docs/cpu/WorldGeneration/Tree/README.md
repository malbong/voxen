# Tree System

<table>
  <tr>
    <td><img src="https://github.com/user-attachments/assets/afdea8f9-83ca-46e9-8b1a-25c5c69bc2e4" width="300"/></td>
    <td><img src="https://github.com/user-attachments/assets/3f942cb7-78c8-47f4-ba6d-5b561e890689" width="300"/></td>
    <td><img src="https://github.com/user-attachments/assets/43a74803-ee33-45db-b9e9-3de2648dc450" width="300"/></td>
  </tr>
  <tr>
    <td><img src="https://github.com/user-attachments/assets/7a293d54-2480-4eff-8a8f-bd759475bb0a" width="300"/></td>
    <td><img src="https://github.com/user-attachments/assets/86badc76-4ab7-48fe-b069-ffe61cb27a43" width="300"/></td>
    <td><img src="https://github.com/user-attachments/assets/2a9767e1-06f1-44a7-ad43-47f9802e35f4" width="300"/></td>
  </tr>
</table>

<br />

## 1. 개요

지표가 결정된 후, 각 청크의 표면 좌표에 8종의 procedural tree를 심는 단계.

지표면에 나무를 심을 수 있는지 판단 후 Biome 별 나무를 심고

트리는 **25³ voxel scratch buffer(`TreeShape`) 위에 형태를 조립**한 뒤 이 결과를 청크의 `m_blocks` 넣는다.
최대 크기가 25 블록이라 청크(32 블록) 경계를 넘어 인접 청크에까지 걸치는 경우가 흔하기에 [Patch 시스템](../../ChunkManagement/README.md)
을 통해 인접 청크에 패치 데이터가 전파된다.

## 2. Tress 구조 — Tree → TreeTypeInfoSet → TreeTypeInfo

[Biome](../Biome/README.md), [Block](../Block/README.md) 과 완전히 같은 정적 테이블 조회 패턴이다.

- `Tree` — **정적 함수만 노출하는 접근 계층**. 개별 인스턴스는 `TREE_TYPE` 한 값만 갖는다.
- `TreeTypeInfoSet` — 256 (실사용 12 언저리) 크기 vector 로 `TreeTypeInfo` 를 담는 컨테이너, 생성자에서 각 타입의 속성 세팅
- `TreeTypeInfo` — 트리의 정보를 담는 객체: 기둥 블록, 잎 블록, shape 관련 파라미터

```cpp
// voxen/headers/Tree.h
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

### 2.1 TreeShapeParams 구조

트리 모양 결정에 사용되는 속성들이다.

```cpp
struct TreeShapeParams {
    int baseHeight;         // 기둥의 기본 높이 (변주는 +랜덤)
    int branchCount;        // 나뭇가지 개수
    int branchLength;       // 나뭇가지 하나당 길이
    int branchStartHeight;  // 가지가 나오는 y 시작점
    int leafRadius;         // 잎 클러스터 반지름
};
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
| JUNGLE   |     19     |      3      |      13      |      13      |     7      |
| ACACIA   |     10     |      1      |      10      |      3       |     6      |

`branchCount=0` 인 OAK / SPRUCE / MANGROVE / BIRCH 는 **기둥 하나 + 그 위의 잎
클러스터** 만 있는 단순 형태이고, `branchCount>0` 인 CHERRY / CACTUS / JUNGLE / ACACIA
는 가지를 여러 번 뻗어 나가는 복잡한 형태다.

### 2.2 왜 OOP 를 쓰지 않았는가

앞선 Biome / Block 문서와 동일한 이유다.

요약: 로직이 다르기보단, 상태 값이 다르다. 구성 로직이 다른 모양 결정은 충분히 switch-case나 if로 구성이 한 곳에 했다.

## 3. Tree 심기 파이프라인 — `Chunk::InitTreePlace`

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

### 3.1 랜덤 위치 선정 — `Terrain::GenerateRandomPlace2D`

청크당 최대 `TREE_PLACE_MAX_COUNT_PER_CHUNK = 64`개의 트리 심을 기준 후보를 xz 2D 격자 내에서 뽑는다.

`TREE_PLACE_MAX_COUNT_PER_CHUNK = 64` 는 상한이며, 실제 배치되는 개수는 biome-별 비율로 따로 계산한다.

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

### 3.2 위치 가능성 검사 — `CanPlaceTreeAt` + `CheckTreePlaceCondition`

트리를 심을 수 있는지 검사하는 필터는 4가지다.

- **[A] 비율 개수 검사** — biome 이 청크를 얼마나 차지하는지에 비례해서 최대 트리 수를 정한다. FOREST 는 청크 전체가 forest 면 최대 32그루, half 만 forest 면 16그루.
- **[B] 밑동은 반드시 청크 내부** — 가장 위에 있는 블록이 청크 내부 위치인지 판단한다. Y에 대한 검사이다.
- **[C] TREE_NONE 처리** — `GetTreeTypeForBiome`에서 `OCEAN` 처럼 트리 없는 biome 이면 `TREE_NONE` 을 반환한다.
- **[D] 3x3 밑동 조건** — 밑동 좌표뿐 아니라 x/z ±1 (십자 5칸) 이 모두 opaque 지면 + 위 공기여야 한다. 절벽 가장자리에 트리가 심어져 공중에 떠 있는 상황을 방지.

```cpp
bool Chunk::CanPlaceTreeAt(int x, int y, int z, uint32_t placedBiomeTreeCount, ...)
{
    // [A] biome 비율에 대한 최대 개수 검사
    BIOME_TYPE biomeType = memory->biomeMap2D[x + 1][z + 1];
    uint32_t maxTreeCountByRatio = GetMaxPlaceCountByBiomeRatio(
        Biome::GetMaxTreeCountPerChunk(biomeType), memory->biomeCount[biomeType]);
    if (placedBiomeTreeCount >= maxTreeCountByRatio) return false;

    // [B] 트리 밑동이 청크 안에 있어야 함
    if (!IsInsideChunk(x, y, z))                     return false;

    // [C] 이 biome 트리 타입을 갖는지
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

### 3.3 트리 종류 결정 — `Tree::GetTreeTypeForBiome`

biome 마다 후보 트리가 이미 [BiomeTypeInfoSet](../Biome/README.md) 에 등록돼 있다.

`Tree::GetTreeTypeForBiome` 는 그 후보 목록에서 `d` (distribution 노이즈) 값에 따라 하나를 선택하는 switch/case 다.

- cf. OOP로 구성하지 않은 점에 대한 단점이 보인다. Biome-Tree가 커플링 되어있다.

후보가 1종인 biome (`TUNDRA`, `PLAINS`, `DESERT`, `TAIGA`, `SNOWY_TAIGA`) 은 항상 그 하나만, 후보가 2종인 biome 은 `d` 값의 threshold 로 확률 결정.

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
    ...
    case BIOME_SWAMP:    return d < 0.3f ? biomeTrees[0] : biomeTrees[1]; // oak / mangrove
    case BIOME_FOREST:   return d < 0.8f ? biomeTrees[0] : biomeTrees[1]; // oak / birch
    case BIOME_SAVANNA:  return d < 0.3f ? biomeTrees[0] : biomeTrees[1]; // oak / acacia
    ...
    }
}
```

### 3.4 트리 심기 — `PlaceTree`

밑동 위치 (`x, y, z`) 가 정해지면, 25³ 임시 buffer 에 형태를 조립하고 청크의 `m_blocks` 로 이전한다.

여기서 트리모양의 결정은 아래에서 조금 더 다룬다.

```cpp
void Chunk::PlaceTree(int x, int y, int z, ChunkLoadMemory* memory, TREE_TYPE treeType)
{
    PosInt3 worldPosInt3 = ...;
    TreeShape treeShape = { TREE_BLOCK_INDEX::EMPTY };
    Tree::GenerateTreeShape(treeType, worldPosInt3, treeShape);      // 모양 생성

    for (int dy = 0; dy < Tree::TREE_SIZE; ++dy)
    for (int dz = 0; dz < Tree::TREE_SIZE; ++dz)
    for (int dx = 0; dx < Tree::TREE_SIZE; ++dx) {
        if (treeShape[dy][dz][dx] == TREE_BLOCK_INDEX::EMPTY) continue;

        // 밑동 좌표 + buffer 좌표 → 청크 로컬 좌표
        int ty = y  +  dy;
        int tz = z  -  dz + (Tree::TREE_SIZE / 2);
        int tx = x  +  dx - (Tree::TREE_SIZE / 2);

        // trunk / leaf → 블록으로
        if (treeShape[dy][dz][dx] == TRUNK || treeShape[dy][dz][dx] == LEAF) {
            BLOCK_TYPE treeBlock = (treeShape[dy][dz][dx] == TRUNK)
                ? Tree::GetTrunkBlockType(treeType)
                : Tree::GetLeafBlockType(treeType);
            SetTreeBlockType(tx, ty, tz, treeBlock, memory);
        }

        // vine 은 instance 로 심음 (블록 아님)
        if ((treeShape[dy][dz][dx] & VINE) == VINE) {
            uint8_t faceFlag = (treeShape[dy][dz][dx] & (~VINE));   // 상위 비트가 방향
            SetTreeVines(tx, ty, tz, INSTANCE_VINE, faceFlag, memory);
        }
    }
}
```

## 4. 모양 결정 알고리즘

### 4.1 `TreeShape` — 25³ 임시 버퍼 사용

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
- xy 방향은 그대로나, 버퍼에서 `+z`는 월드 기준 `-z`다.

### 4.2 `Tree::GenerateTreeShape` — 5개 생성 함수 dispatch

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

여러 종이 공유하는 나무 모양을 구성하는 방식은 크게 4가지다.

#### (a) 기둥 (Trunk)

가장 단순한 경우 (`GenerateBasicTree`): 중심 (12, 12) 에서 y 방향으로 baseHeight ± 랜덤 만큼 TRUNK 를 채운다.

```cpp
int heightRange = params.baseHeight + Utils::RandomRangeByPos(worldPos, -1, 1);
for (int y = 0; y < heightRange; ++y)
    tree[y][tz][tx] = TREE_BLOCK_INDEX::TRUNK;
```

#### (b) 나뭇가지 (Branch)

두 가지 진행 방식이 있다.

**Gradient branch** (`AddBranchForGradient`) — CHERRY / JUNGLE / ACACIA 에서 사용.
랜덤한 방향 gradient을 선택하고 축 정렬된 6방향 후보 중 gradient와 내적하여 방향을 매 스텝 선택한다.
결과적으로 gradient 방향으로 뻗어나가는 branch가 구성된다.

```cpp
Vector3 curDir = GenerateNextDirectionForGradient(diffPos, gradient);
```

**Random branch** (`AddBranchForRandom`) — CACTUS 에서 사용.
6방향 중 역방향/이전 역방향/초기 역방향을 제외한 랜덤 방향으로 진행. y 축 방향은 특정 loop 이후에만 허용해 처음엔 수평, 나중엔 위로 굽는 형태.

가지 하나의 마지막 위치(`lastBranchPos`) 는 잎 클러스터의 중심이 된다.

#### (c) 잎 클러스터 (Leaf Cluster / Plane)

**AddLeafCluster** — 3D 타원구를 채운다. `shrink` 로 타원구의 비율을 축방향으로 줄일 때 사용한다,
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
    if tree[y][z][x] != LEAF && tree[y][z][x] != TRUNK: continue // 덩굴이나 잎사귀가 나올때까지

    for d in {L, R, F, B}:
        nx, nz = neighbor in direction d      // 주변이
        if tree[y][nz][nx] != EMPTY: continue // 비어있는지 체크
        if hash(world position) % 100 <= vinePercent: // 퍼센트 기반 확률로 Vines 설정
            length = random(1, y)
            DropVines(nx, y, nz, d, length, tree)   // 아래로 length 칸 늘어뜨림
```

`DropVines` 는 (nx, y, nz) 부터 y 감소 방향으로 length 칸까지 진행하며 EMPTY 인 칸에
`VINE | V_LEFT/RIGHT/FRONT/BACK` 를 덧씌운다. `d` 는 어느 트리 면에서 나온 vine 인지를 기록해서
나중에 렌더링할 때 vine 이 벽에 붙는 방향을 결정한다.

- 방향: 트리에서 왼쪽으로 나온 vine 은 그 vine 을 vine 셀 기준으로 보면 오른쪽에 트리가 있으므로 `V_RIGHT` (트리가 붙은 방향) 로 저장.
- **JUNGLE** 은 `vinePercent = 5 + RandomRange(0, 5)` 로 드물게, **MANGROVE** 는 `40 + RandomRange(-10, 10)` 로 매우 촘촘하게 덩굴을 늘어뜨린다.

### 4.3 각 트리 타입의 조합

| 타입       | 함수              | trunk      | branch                  | leaf                              | vine       |
| ---------- | ----------------- | ---------- | ----------------------- | --------------------------------- | ---------- |
| OAK, BIRCH | GenerateBasicTree | 1칸, ±1    | 없음                    | Cluster (타원구) + cap Cluster    | 없음       |
| SPRUCE     | GenerateSpruce    | 1칸, ±2/+4 | 없음                    | Cluster (y-2.5배 늘림) + cap      | 없음       |
| MANGROVE   | GenerateMangrove  | 1칸, ±0/-1 | 없음                    | Cluster (y-1.5 배)                | 40% (많음) |
| CHERRY     | GenerateCherry    | 1칸, ±1    | Gradient × 2            | 각 가지 끝 Cluster + 본체 Cluster | 없음       |
| CACTUS     | GenerateCactus    | 1칸, ±1    | Random × 2 (수평 후 위) | 없음                              | 없음       |
| JUNGLE     | GenerateJungle    | 2x2, ±2    | Gradient × 3            | 각 가지 끝 Plane + 본체 3층 Plane | 5%(적음)   |
| ACACIA     | GenerateAcacia    | 1칸, ±1    | Gradient × 1            | 가지 끝 Plane + 본체 2층 Plane    | 없음       |

## 5. 청크 경계 처리 — Patch 전파

트리 크기가 최대 25 블록인데 청크 하나는 32 블록이므로, 청크 가장자리 경계에서 트리가 주변에 퍼질 수 있다.
A 청크가 다른 청크에 직접 값을 쓸 수 없고, 대신 **패치 데이터를 만들어 두면 이후 Patch 파이프라인이 인접 청크에 반영**한다.

### 5.1 `SetTreeBlockType` — 3-way 분기

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

    // [3] 청크 안밖의 (Inner/Outer) Edge 이면 그리디 메싱을 위해 인접 청크에도 patch (본인을 수정하는 패치는 제외)
    if (IsInnerEdge(tx, ty, tz) || IsOuterEdge(tx, ty, tz)) {
        PatchData::GenerateEdgePatchEntry(...);            // 최대 3개 인접 청크
        for each entry: memory->loadPatchResult[...].insert(patchData);
    }
}
```

- `IsInsideChunkWithPadding` 은 `-1 ≤ x ≤ 32` 로 padding 을 포함한 범위 (`CHUNK_SIZE_P` 배열의 유효 인덱스)
- `!IsInsideChunk` 는 청크의 32×32×32 영역 밖. **인접 소유 청크(blockOwnerOffsetPos) 를 계산해서 Patch Data로 구성한다.**
- 안쪽과 바깥쪽 Edge 블록은 인접 청크의 mesh 도 영향을 받으므로 (자기 청크 안에 있어도) patch 를 함께 보낸다.

### 5.2 Vine 은 instance 로 patch

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

- Vine 은 블록이 아니라 **instance** (단순 사각형 메쉬) 로 렌더링되므로 별도의 `m_instanceMap` 에 들어간다.
- `faceFlag` 는 vine 이 붙은 트리 면의 방향으로, ChunkManager에서 vine quad 를 어느 벽 쪽으로 붙일지 결정.
- 청크 밖 vine 도 마찬가지로 patch 데이터로 보낸다.

### 5.3 로드 시점의 패치 큐 → Patch 파이프라인

`memory->loadPatchResult` 는 `PosHashMap<PatchDataHashSet>` (청크 offset → 패치 세트) 이다.
`Chunk::Initialize` 가 리턴한 후 ChunkManager 가 이 결과를 모아서 [Patch 파이프라인](../../ChunkManagement/README.md) 으로 넘긴다.

### 6. 트리 요약

1. 기본 블록 구성이 완료된 청크를 바탕으로 연산
2. 후보군 선정 `GenerateRandomPlace2D`
3. 트리 심을 수 있는지 검사 `CanPlaceTreeAt`
4. 트리 심기 `PlaceTree`
5. 트리 모양 결정 `Tree::GenerateTreeShape`
6. Block, Instance 구분 -> 패치 전파

## 6. 회고

Patch 로직이 없었을 때 진행했기에 가장 어려웠던 CPU 구현 챕터였다.

- Tree를 구성하려보니 Tree 관련 로직만 작성하면 되는게 아니라, 전반적인 Patch라는 구조가 추가되는 복잡성이 있었다
  - Patch 멀티쓰레드 구성, 동기화, 안정성 고려
  - ChunkManager 구조 변경
  - Patch Data 구성
- Tree를 단순히 NxNxN 배열에서 정적으로 트리를 구성하는건 큰 의미가 없다고 판단해서 Tree Shape 결정에 신경을 많이썼다.
  - 기둥, 가지, 잎사귀, 덩굴 등 따로 함수로 구성할 수 있어서 확장성은 나쁘지 않았다.
  - 하지만, 테스트를 위해서는 많은 빌드 실행의 결과가 있었다.

아쉬운 점

- 트리 모양 결정에서 사용되는 Hash기반 랜덤 Range 선택이 머리속에 남는다.
- Hash 기반 랜덤 Range 선택은 느릴 것 같은게 눈에 보이고 심지어 루프까지 도는 상황이였다.
- 이전 해쉬값을 사용하면 성능 개선에 영향을 줄 것이다.

전반적인 결과에 만족한다.

- 다양한 트리 타입 및 다양한 트리 모양이 존재한다.
- 청크 주변에서의 Patch도 적절하다. 문제 없었다.
