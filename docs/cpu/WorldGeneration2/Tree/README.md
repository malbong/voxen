# Tree

## 1. 개요

Tree 시스템은 Voxen 엔진에서 8종의 절차적 나무 구조를 생성하고 월드에 배치하는 모듈입니다. 25x25x25 크기의 3D 배열(`TreeShape`)에 나무 형태를 생성한 후, 청크 시스템과 연동하여 블록으로 변환합니다.

엔진 전체에서 다음 역할을 수행합니다:
- **바이옴별 나무 선택**: `Tree::GetTreeTypeForBiome()`으로 바이옴과 distribution 노이즈에 따라 나무 종류 결정
- **절차적 나무 생성**: `Tree::GenerateTreeShape()`로 각 나무 타입별 고유한 형태(trunk, leaf, vine) 생성
- **청크 간 나무 배치**: `Chunk::InitTreePlace()`에서 나무 위치를 결정하고, 청크 경계를 넘는 나무도 패치 시스템으로 처리
- **다양한 나무 형태**: 기본 나무(Oak, Birch), 침엽수(Spruce), 가지형(Cherry, Acacia, Jungle), 선인장(Cactus), 덩굴형(Mangrove) 등 8종 지원

## 2. 도입 동기

마인크래프트와 같은 자연스럽고 다양한 식생을 목표로 했습니다.

### 기존 단순 배치 방식의 한계
1. **단일 나무 형태**: 모든 나무가 동일한 모양으로 단조로움
2. **바이옴 무시**: 사막에도 떡갈나무, 툰드라에도 정글 나무 등 비현실적 배치
3. **청크 경계 문제**: 나무가 청크 경계를 넘을 때 잘리거나 중복 생성
4. **수동 디자인 필요**: 새로운 나무 추가 시 수작업으로 블록 배치 정의 필요

### 목표
- **절차적 생성**: 파라미터 기반 나무 생성으로 동일 종류도 매번 다른 형태
- **바이옴 연계**: 각 바이옴에 어울리는 나무 종류와 비율 자동 결정
- **청크 간 일관성**: 나무가 여러 청크에 걸쳐도 올바르게 생성되도록 패치 시스템 구축
- **확장성**: 새로운 나무 타입 추가 시 파라미터만 정의하면 자동 생성

## 3. 핵심 아이디어

### 3.1. TreeShape 3D 배열 기반 생성

나무를 25x25x25 크기의 3D 배열(`uint8_t[25][25][25]`)에 먼저 생성한 후 월드 블록으로 변환:

```cpp
using TreeShape = uint8_t[25][25][25];
```

**장점**:
- **독립적 생성**: 월드 좌표와 무관하게 나무 형태를 먼저 설계
- **청크 경계 자유**: 25x25x25 크기로 청크(32x32x32)를 넘는 대형 나무 생성 가능
- **재사용성**: 동일한 TreeShape를 여러 위치에 복사 가능 (추후 확장)

**TreeShape 값**:
```cpp
enum TREE_BLOCK_INDEX {
    EMPTY = 0,
    TRUNK = 1,
    LEAF = 2,
    VINE = 0x80  // bit flag: 0b10000000
};
```

- `EMPTY(0)`: 빈 공간
- `TRUNK(1)`: 나무 몸통 블록
- `LEAF(2)`: 나무 잎 블록
- `VINE(0x80)`: 덩굴 인스턴스 플래그 + 방향 비트 (4방향: V_LEFT, V_RIGHT, V_FRONT, V_BACK)

### 3.2. TreeShapeParams 파라미터 기반 생성

각 나무 타입은 5개 파라미터로 정의:

```cpp
struct TreeShapeParams {
    int baseHeight;         // 기본 몸통 높이
    int branchCount;        // 가지 개수
    int branchLength;       // 가지 길이
    int branchStartHeight;  // 가지 시작 높이
    int leafRadius;         // 잎 반경
};
```

**8종 나무 타입별 파라미터**:

| 나무 타입   | baseHeight | branchCount | branchLength | branchStartHeight | leafRadius | 특징                          |
|-----------|------------|-------------|--------------|-------------------|-----------|-------------------------------|
| OAK       | 6          | 0           | 0            | 0                 | 3         | 기본 구형 잎                    |
| BIRCH     | 5          | 0           | 0            | 0                 | 2         | 작고 짧은 나무                  |
| SPRUCE    | 11         | 0           | 0            | 0                 | 4         | 원뿔형 침엽수                   |
| MANGROVE  | 6          | 0           | 0            | 0                 | 3         | 넓은 잎 + 덩굴 40%              |
| CHERRY    | 8          | 2           | 13           | 3                 | 6         | 가지 2개 + 큰 구형 잎            |
| CACTUS    | 6          | 2           | 4            | 4                 | 0         | 선인장 (잎 없음)                |
| JUNGLE    | 18         | 3           | 13           | 13                | 7         | 2x2 trunk + 가지 3개 + 덩굴 5~10% |
| ACACIA    | 9          | 1           | 10           | 3                 | 6         | 가지 1개 + 평면 잎              |

**랜덤 변형**:
모든 나무는 월드 좌표 기반 시드로 파라미터에 ±1~2 범위의 랜덤 변형 적용:
```cpp
int heightRange = params.baseHeight + Utils::RandomRangeByPos(worldPos, -1, 1);
```
→ 동일 타입도 매번 다른 높이/크기 생성

### 3.3. 바이옴별 나무 선택 로직

`GetTreeTypeForBiome()`은 바이옴과 distribution 노이즈로 나무 타입 결정:

```cpp
TREE_TYPE GetTreeTypeForBiome(BIOME_TYPE biome, float d, int x, int y, int z) {
    const std::vector<TREE_TYPE> biomeTrees = Biome::GetTrees(biome);

    switch (biome) {
    case BIOME_SWAMP:
        if (d < 0.3f) return biomeTrees[0];  // Oak 70%
        else return biomeTrees[1];           // Mangrove 30%

    case BIOME_FOREST:
        if (d < 0.8f) return biomeTrees[0];  // Oak 80%
        else return biomeTrees[1];           // Birch 20%

    case BIOME_DESERT:
        return biomeTrees[0];  // Cactus 100%

    case BIOME_SAVANNA:
        if (d < 0.3f) return biomeTrees[0];  // Oak 30%
        else return biomeTrees[1];           // Acacia 70%

    // ... 다른 바이옴들
    }
}
```

**특징**:
- **Distribution 노이즈**: 0.0~1.0 범위의 세밀한 노이즈(scale 24)로 나무 종류 비율 결정
- **바이옴별 나무 풀**: `Biome::GetTrees(biome)`이 반환한 나무 리스트에서 선택
- **단일 타입 바이옴**: DESERT(Cactus), TUNDRA(Spruce) 등은 distribution 무시하고 단일 타입만 생성

### 3.4. 청크 간 나무 배치 및 패치 전파

나무는 25x25x25 크기로 청크(32x32x32)를 넘을 수 있으므로 패치 시스템 사용:

**배치 흐름**:
1. `Chunk::InitTreePlace()`: 청크별로 나무 위치 결정
   - `Terrain::GenerateRandomPlace2D()`로 청크 내 랜덤 위치(X, Z) 생성
   - `CanPlaceTreeAt()`으로 배치 가능 여부 검사
   - `PlaceTree()`로 TreeShape 생성 및 블록 배치

2. `PlaceTree()`: TreeShape를 월드 블록으로 변환
   - `Tree::GenerateTreeShape()`로 25x25x25 배열 생성
   - 각 TRUNK/LEAF 블록을 `SetTreeBlockType()`으로 배치
   - 각 VINE 플래그를 `SetTreeVines()`로 인스턴스 배치

3. `SetTreeBlockType()`: 블록 설정 + 패치 전파
   ```cpp
   void SetTreeBlockType(int tx, int ty, int tz, BLOCK_TYPE treeBlock, ChunkLoadMemory* memory) {
       // 1. 청크 내부면 직접 설정
       if (IsInsideChunkWithPadding(tx, ty, tz)) {
           m_blocks[tx + 1][ty + 1][tz + 1].SetType(treeBlock);
       }

       // 2. 청크 외부면 PatchData 생성
       if (!IsInsideChunk(tx, ty, tz)) {
           PatchData patchData = MakePatchData(tx, ty, tz, treeBlock, ...);
           Vector3 blockOwnerOffsetPos = CalcOffsetPos(blockPos, CHUNK_SIZE);
           memory->chunkPatchDataMap[blockOwnerOffsetPosInt3].insert(patchData);
       }

       // 3. Edge 블록이면 인접 청크에도 패치 전파 (Greedy Meshing 대비)
       if (IsInnerEdge(tx, ty, tz) || IsOuterEdge(tx, ty, tz)) {
           GenerateEdgePatchEntry(...);
       }
   }
   ```

**핵심**:
- **ChunkLoadMemory의 chunkPatchDataMap**: `map<PosInt3, PatchDataHashSet>` 구조로 인접 청크별 패치 데이터 저장
- **3단계 패치**: (1) 현재 청크 직접 설정, (2) 외부 청크 패치 생성, (3) Edge 블록 인접 패치
- **Greedy Meshing 대비**: Edge 블록은 메쉬 생성 시 인접 청크 블록 참조 필요하므로 패치 전파

## 4. 구현 내용

### 4.1. 나무 형태 생성 알고리즘

#### 4.1.1. 기본 나무 (Oak, Birch) - `GenerateBasicTree()`

**알고리즘**:
```cpp
void GenerateBasicTree(const TreeShapeParams& params, const PosInt3& worldPos, TreeShape& tree) {
    int tx = TREE_SIZE / 2;  // 중앙: 12
    int tz = TREE_SIZE / 2;

    // 1. 수직 trunk 생성
    int heightRange = params.baseHeight + RandomRangeByPos(worldPos, -1, 1);  // 5~7
    for (int y = 0; y < heightRange; ++y)
        tree[y][tz][tx] = TRUNK;

    // 2. 메인 잎 클러스터 (ellipsoid)
    Vector3 center = Vector3(tx, heightRange - 2, tz);
    Vector3 shrink = Vector3(1.0f, 2.0f, 1.0f);  // Y축 늘림
    int leafRadius = params.leafRadius + RandomRangeByPos(worldPos, 0, 1);  // 3~4
    AddLeafCluster(center, shrink, leafRadius, -leafRadius, leafRadius, 0.125f, worldPos, tree);

    // 3. 상단 캡 잎 (작은 반구)
    center = Vector3(tx, heightRange - 1, tz);
    shrink = Vector3(1.0f, 1.5f, 1.0f);
    AddLeafCluster(center, shrink, 2, 0, 0, 0.25f, worldPos, tree);
}
```

**AddLeafCluster() - 타원체 잎 생성**:
```cpp
void AddLeafCluster(Vector3 center, Vector3 shrink, int radius,
                    int carveStartY, int carveEndY, float carveScale,
                    const PosInt3 worldPos, TreeShape& tree) {
    for (int y = -radius; y <= radius; ++y) {
        float sy = y * shrink.y;
        for (int z = -radius; z <= radius; ++z) {
            float sz = z * shrink.z;
            for (int x = -radius; x <= radius; ++x) {
                float sx = x * shrink.x;

                // Carving: Y축 특정 범위에서 랜덤 불규칙 적용
                if (carveStartY <= y && y <= carveEndY) {
                    uint32_t hash = HashInt(worldX * worldY * worldZ, 100043u);
                    sx *= (1.0f + (carveScale * (hash % 2)));  // 0% or 12.5% 확장
                    sz *= (1.0f + (carveScale * (hash % 2)));
                }

                // Ellipsoid 공식: (x/a)^2 + (y/b)^2 + (z/c)^2 < r^2
                if (sx*sx + sy*sy + sz*sz < radius*radius) {
                    int nx = center.x + x;
                    int ny = center.y + y;
                    int nz = center.z + z;

                    if (tree[ny][nz][nx] == EMPTY)
                        tree[ny][nz][nx] = LEAF;
                }
            }
        }
    }
}
```

**결과**:
- Oak: 높이 5~7, 타원형 잎(반경 3~4) + 상단 작은 잎
- Birch: 높이 4~6, 작은 타원형 잎(반경 2~3)

#### 4.1.2. 침엽수 (Spruce) - `GenerateSpruce()`

**알고리즘**:
```cpp
void GenerateSpruce(const TreeShapeParams& params, const PosInt3& worldPos, TreeShape& tree) {
    int heightRange = params.baseHeight + RandomRangeByPos(worldPos, -2, 4);  // 9~15

    // 1. Trunk
    for (int y = 0; y < heightRange; ++y)
        tree[y][tz][tx] = TRUNK;

    // 2. 원뿔형 잎 (Y축 2.5배 늘림)
    Vector3 center = Vector3(tx, heightRange - 4, tz);
    Vector3 shrink = Vector3(1.0f, 2.5f, 1.0f);  // 세로로 긴 타원
    int leafRadius = params.leafRadius + RandomRangeByPos(worldPos, -1, 1);  // 3~5
    int carveStartY = -(1 + radiusRange);
    int carveEndY = radiusRange;
    AddLeafCluster(center, shrink, leafRadius, carveStartY, carveEndY, 0.25f, worldPos, tree);

    // 3. 상단 첨탑 잎
    center = Vector3(tx, heightRange - 1, tz);
    shrink = Vector3(1.0f, 1.5f, 1.0f);
    AddLeafCluster(center, shrink, 2, -1, 1, 0.25f, worldPos, tree);
}
```

**결과**: 높이 9~15, Y축으로 긴 타원체(shrink.y=2.5) → 원뿔형 실루엣

#### 4.1.3. 가지형 나무 (Cherry, Acacia, Jungle)

**공통 구조**:
1. 수직 trunk 생성
2. `branchCount`만큼 가지 생성 (gradient 또는 랜덤 방향)
3. 각 가지 끝에 잎 클러스터 배치
4. 중앙 trunk 상단에 메인 잎 배치

**Cherry 예시**:
```cpp
void GenerateCherry(const TreeShapeParams& params, const PosInt3& worldPos, TreeShape& tree) {
    int heightRange = params.baseHeight + RandomRangeByPos(worldPos, -1, 1);  // 7~9

    // 1. Trunk
    for (int y = 0; y < heightRange; ++y)
        tree[y][tz][tx] = TRUNK;

    // 2. 가지 2개 생성
    for (int i = 0; i < params.branchCount; ++i) {  // branchCount = 2
        int branchLength = params.branchLength + RandomRangeByPosForLoop(worldPos, i, -1, 1);  // 12~14
        int branchStartHeight = params.branchStartHeight + RandomRangeByPosForLoop(worldPos, i, -1, 1);  // 2~4

        // Gradient 생성: (2~6, 0~1, 2~6) 범위의 랜덤 방향 벡터
        Vector3 minScale = Vector3(2.0f, 0.0f, 2.0f);
        Vector3 maxScale = Vector3(6.0f, 1.0f, 6.0f);
        Vector3 gradient = GenerateRandomGradient(worldPos, i, minScale, maxScale);

        // Gradient 방향으로 가지 생성
        Vector3 lastBranchPos = AddBranchForGradient(
            worldPos, branchLength, branchStartHeight, gradient, tree);

        // 가지 끝에 타원체 잎 배치
        Vector3 shrink = Vector3(1.25f, 2.0f, 1.25f);
        int leafRadius = 3;
        AddLeafCluster(lastBranchPos, shrink, leafRadius, -leafRadius, leafRadius, 0.25f, worldPos, tree);
    }

    // 3. 중앙 메인 잎 (큰 타원체)
    Vector3 center = Vector3(tx, heightRange - 1, tz);
    Vector3 shrink = Vector3(1.25f, 2.75f, 1.25f);  // Y축 2.75배
    int leafRadius = params.leafRadius + RandomRangeByPos(worldPos, 0, 1);  // 6~7
    AddLeafCluster(center, shrink, leafRadius, -leafRadius, leafRadius, 0.075f, worldPos, tree);
}
```

**AddBranchForGradient() - Gradient 추적 가지 생성**:
```cpp
Vector3 AddBranchForGradient(const PosInt3& worldPos, int branchLength, int startHeight,
                              Vector3 gradient, TreeShape& tree) {
    int nx = TREE_SIZE / 2;
    int nz = TREE_SIZE / 2;
    int ny = startHeight;

    Vector3 diffPos = Vector3(0.0f);  // 현재 위치 - 목표 위치
    Vector3 curDir = GenerateNextDirectionForGradient(diffPos, gradient);

    for (int j = 0; j < branchLength; ++j) {
        nx += (int)curDir.x;
        ny += (int)curDir.y;
        nz += (int)curDir.z;

        if (tree[ny][nz][nx] == EMPTY) {
            tree[ny][nz][nx] = TRUNK;
            lastBranchPos = Vector3(nx, ny, nz);
        }

        diffPos += curDir;
        curDir = GenerateNextDirectionForGradient(diffPos, gradient);  // Gradient 방향으로 다음 스텝
    }

    return lastBranchPos;
}
```

**GenerateNextDirectionForGradient() - Gradient 방향 결정**:
```cpp
Vector3 GenerateNextDirectionForGradient(Vector3 diffPos, Vector3 gradient) {
    Vector3 dir[6] = { {-1,0,0}, {1,0,0}, {0,-1,0}, {0,1,0}, {0,0,1}, {0,0,-1} };

    gradient.Normalize();

    Vector3 bestDir(0.0f);
    float bestScalar = -1.0f;

    for (int i = 0; i < 6; ++i) {
        Vector3 nextDiffPos = diffPos + dir[i];

        // 목표(gradient)에서 멀어지는 방향 제외
        if (diffPos.Length() >= nextDiffPos.Length())
            continue;

        nextDiffPos.Normalize();
        float scalar = gradient.Dot(nextDiffPos);  // 내적: gradient와 방향 유사도

        if (scalar > bestScalar) {
            bestDir = dir[i];
            bestScalar = scalar;
        }
    }

    return bestDir;
}
```

**핵심**:
- **Gradient**: 랜덤 3D 벡터로 가지가 뻗어나갈 대략적 방향 지정
- **Greedy 방향 선택**: 6방향 중 gradient와 내적이 최대인 방향 선택 → gradient 방향으로 점진적 접근
- **결과**: 부드러운 곡선 가지 (매 스텝마다 gradient 쪽으로 조금씩 이동)

#### 4.1.4. 정글 나무 - `GenerateJungle()`

**특수 구조**:
- **2x2 Trunk**: 단일 블록이 아닌 2x2 단면의 굵은 몸통
- **3개 가지**: branchCount=3, branchLength=13
- **평면 잎**: `AddLeafPlane()`으로 Y축 단일 레이어 원형 잎 생성
- **덩굴**: `AddVines(vinePercent=5~10%)`로 낮은 확률 덩굴 배치

```cpp
void GenerateJungle(const TreeShapeParams& params, const PosInt3& worldPos, TreeShape& tree) {
    int heightRange = params.baseHeight + RandomRangeByPos(worldPos, -2, 2);  // 16~20

    // 1. 2x2 Trunk
    for (int y = 0; y < heightRange; ++y) {
        tree[y][tz + 0][tx + 0] = TRUNK;
        tree[y][tz - 1][tx + 1] = TRUNK;
        tree[y][tz - 1][tx + 0] = TRUNK;
        tree[y][tz + 0][tx + 1] = TRUNK;
    }

    // 2. 가지 3개 + 가지 끝 평면 잎
    for (int i = 0; i < params.branchCount; ++i) {
        Vector3 gradient = GenerateRandomGradient(worldPos, i, Vector3(1,1,1), Vector3(6,3,6), true);
        Vector3 lastBranchPos = AddBranchForGradient(worldPos, branchLength, branchStartHeight, gradient, tree);

        // 가지 끝에 2층 평면 잎
        AddLeafPlane(lastBranchPos, Vector3(1.25f, 1.0f, 1.25f), 3, true, 0.075f, worldPos, tree);
        lastBranchPos.y += 1;
        AddLeafPlane(lastBranchPos, Vector3(1.25f, 1.0f, 1.25f), 2, true, 0.125f, worldPos, tree);
    }

    // 3. 중앙 상단 평면 잎 3층
    Vector3 center = Vector3(tx, heightRange, tz);
    AddLeafPlane(center, Vector3(1.25f, 1.0f, 1.25f), leafRadius - 1, true, 0.125f, worldPos, tree);
    center.y -= 1;
    AddLeafPlane(center, Vector3(1.25f, 1.0f, 1.25f), leafRadius, true, 0.075f, worldPos, tree);
    center.y -= 1;
    AddLeafPlane(center, Vector3(1.25f, 1.0f, 1.25f), leafRadius - 3, true, 0.075f, worldPos, tree);

    // 4. 덩굴 5~10%
    AddVines(worldPos, 5 + RandomRangeByPos(worldPos, 0, 5), tree);
}
```

#### 4.1.5. 선인장 - `GenerateCactus()`

**특징**:
- **잎 없음**: leafRadius=0
- **랜덤 방향 가지**: `AddBranchForRandom()`으로 X/Z 방향 랜덤 가지

```cpp
void GenerateCactus(const TreeShapeParams& params, const PosInt3& worldPos, TreeShape& tree) {
    int heightRange = params.baseHeight + RandomRangeByPos(worldPos, -1, 1);  // 5~7

    // 1. Trunk
    for (int y = 0; y < heightRange; ++y) {
        tree[y][tz][tx] = TRUNK;
    }

    // 2. 가지 2개 (랜덤 방향)
    for (int i = 0; i < params.branchCount; ++i) {
        int branchLength = params.branchLength + RandomRangeByPosForLoop(worldPos, i, -1, 1);  // 3~5
        int branchStartHeight = params.branchStartHeight + RandomRangeByPosForLoop(worldPos, i, -1, 0);  // 3~4

        AddBranchForRandom(worldPos, branchLength, branchStartHeight, i, 1, tree);
    }
}
```

**AddBranchForRandom() - 랜덤 방향 가지**:
```cpp
Vector3 AddBranchForRandom(const PosInt3& worldPos, int branchLength, int startHeight,
                           int loop, int startYDir, TreeShape& tree) {
    Vector3 initDir = GenerateRandomBasisDirection2D(worldPos, loop);  // 4방향 중 하나: ±X, ±Z
    Vector3 curDir = initDir;
    Vector3 prevDir = Vector3(0.0f);

    for (int j = 0; j < branchLength; ++j) {
        nx += curDir.x;
        ny += curDir.y;
        nz += curDir.z;

        if (tree[ny][nz][nx] == EMPTY) {
            tree[ny][nz][nx] = TRUNK;
        }

        // 다음 방향: 현재 방향의 역방향, 이전 방향의 역방향, 초기 방향의 역방향 제외
        curDir = GenerateNextDirectionForRandom(worldPos, curDir, prevDir, initDir, j, startYDir);
        prevDir = tmpDir;
    }
}
```

**GenerateNextDirectionForRandom() - 랜덤 방향 결정**:
```cpp
Vector3 GenerateNextDirectionForRandom(const PosInt3& worldPos, Vector3 curDir, Vector3 prevDir,
                                       Vector3 initDir, int loop, int startDirY) {
    Vector3 dir[6] = { {-1,0,0}, {1,0,0}, {0,-1,0}, {0,1,0}, {0,0,1}, {0,0,-1} };

    std::vector<Vector3> possibleDirs;
    for (int i = 0; i < 6; ++i) {
        // 역방향 제외
        if (dir[i] == -curDir || dir[i] == -prevDir || dir[i] == -initDir)
            continue;

        // Y축 방향은 startDirY 이후에만 허용
        if (i == DIR::BOTTOM || i == DIR::TOP) {
            if (loop >= startDirY) {
                possibleDirs.push_back(dir[i]);
            }
        } else {
            possibleDirs.push_back(dir[i]);
        }
    }

    int chooseIndex = RandomRangeByPosForLoop(worldPos, loop, 0, possibleDirs.size() - 1);
    return possibleDirs[chooseIndex];
}
```

**결과**: 선인장 특유의 구불구불한 가지 (X/Z 중심, 중간부터 Y 방향 허용)

### 4.2. 덩굴(Vine) 시스템

**AddVines() - 잎/trunk 주변에 덩굴 생성**:
```cpp
void AddVines(const PosInt3& worldPos, int vinePercent, TreeShape& tree) {
    int dir[4][2] = { {-1,0}, {1,0}, {0,1}, {0,-1} };  // L R F B

    for (int y = TREE_SIZE - 1; y >= 0; --y) {  // 위에서 아래로
        for (int z = 0; z < TREE_SIZE; ++z) {
            for (int x = 0; x < TREE_SIZE; ++x) {
                if (tree[y][z][x] == LEAF || tree[y][z][x] == TRUNK) {

                    // 4방향 인접 블록 검사
                    for (int d = 0; d < 4; ++d) {
                        int nx = x + dir[d][0];
                        int nz = z + dir[d][1];

                        if (tree[y][nz][nx] == EMPTY) {
                            int percent = RandomRangeByPos(worldPosInt3, 0, 100);

                            if (percent <= vinePercent) {
                                int length = RandomRangeByPos(worldPosInt3, 1, y + 1);
                                DropVines(nx, y, nz, d, length, tree);
                            }
                        }
                    }
                }
            }
        }
    }
}
```

**DropVines() - Y축 아래로 덩굴 드롭**:
```cpp
void DropVines(int x, int y, int z, int dir, int height, TreeShape& tree) {
    for (int ty = y; ty > y - height; --ty) {
        if (tree[ty][z][x] == LEAF || tree[ty][z][x] == TRUNK)
            return;  // 잎/trunk 만나면 중단

        // 방향별 플래그 설정
        if (dir == 0) {  // LEFT -> V_RIGHT 방향 덩굴
            tree[ty][z][x] |= (VINE | (1 << V_RIGHT));
        } else if (dir == 1) {  // RIGHT -> V_LEFT
            tree[ty][z][x] |= (VINE | (1 << V_LEFT));
        } else if (dir == 2) {  // FRONT -> V_BACK
            tree[ty][z][x] |= (VINE | (1 << V_BACK));
        } else if (dir == 3) {  // BACK -> V_FRONT
            tree[ty][z][x] |= (VINE | (1 << V_FRONT));
        }
    }
}
```

**VINE 플래그 구조**:
```cpp
// Bit layout: 0b1_0000_FBRL
// Bit 7: VINE flag
// Bit 0-3: V_RIGHT, V_LEFT, V_BACK, V_FRONT
```

**결과**:
- Mangrove: 40% 확률로 덩굴 생성 → 맹그로브 숲 분위기
- Jungle: 5~10% 확률 → 정글의 덩굴 장식

### 4.3. Chunk 배치 시스템

**Chunk::InitTreePlace() - 나무 배치**:
```cpp
void Chunk::InitTreePlace(ChunkLoadMemory* memory) {
    // 1. 랜덤 위치 생성 (청크당 최대 TREE_PLACE_MAX_COUNT_PER_CHUNK)
    Terrain::GenerateRandomPlace2D(m_offsetPosition, TREE_PLACE_RANDOM_SOLT_X,
        TREE_PLACE_RANDOM_SOLT_Z, TREE_PLACE_MAX_COUNT_PER_CHUNK, CHUNK_SIZE,
        memory->treeRandomPlace2D);

    uint32_t placedBiomeTreeCount[Biome::BIOME_TYPE_COUNT] = {0};

    for (int i = 0; i < TREE_PLACE_MAX_COUNT_PER_CHUNK; ++i) {
        int x = memory->treeRandomPlace2D[i].first;
        int z = memory->treeRandomPlace2D[i].second;

        BIOME_TYPE biomeType = memory->biomeMap2D[x][z];
        float elevation = memory->elevationNoises[x + 1][z + 1];
        int localY = (int)(floor(elevation) - m_offsetPosition.y);

        // 2. 배치 가능 여부 검사
        if (CanPlaceTreeAt(x, localY, z, placedBiomeTreeCount[biomeType], memory)) {
            TREE_TYPE treeType = Tree::GetTreeTypeForBiome(
                biomeType, memory->distributionNoises[x + 1][z + 1], x, localY, z);

            // 3. 나무 생성 및 블록 배치
            PlaceTree(x, localY, z, memory, treeType);

            placedBiomeTreeCount[biomeType]++;
        }
    }
}
```

**CanPlaceTreeAt() - 배치 조건**:
```cpp
bool Chunk::CanPlaceTreeAt(int x, int y, int z, uint32_t placedBiomeTreeCount, ChunkLoadMemory* memory) {
    // 1. 바이옴별 최대 나무 개수 체크
    BIOME_TYPE biomeType = memory->biomeMap2D[x][z];
    uint32_t maxTreeCountByRatio = GetMaxPlaceCountByBiomeRatio(
        biomeType, Biome::GetMaxTreeCountPerChunk(biomeType), memory->biomeCount[biomeType]);
    if (placedBiomeTreeCount >= maxTreeCountByRatio) {
        return false;
    }

    // 2. 청크 내부 여부
    if (!IsInsideChunk(x, y, z)) {
        return false;
    }

    // 3. 나무 타입 유효성
    TREE_TYPE treeType = Tree::GetTreeTypeForBiome(biomeType, memory->distributionNoises[x+1][z+1], x, y, z);
    if (treeType == TREE_TYPE::TREE_NONE) {
        return false;
    }

    // 4. 주변 3x3 배치 조건
    for (int i = -1; i <= 1; ++i) {
        if (!CheckTreePlaceCondition(x + i, y, z)) return false;
        if (!CheckTreePlaceCondition(x, y, z + i)) return false;
    }

    return true;
}
```

**CheckTreePlaceCondition() - 단일 블록 배치 조건**:
```cpp
bool Chunk::CheckTreePlaceCondition(int x, int y, int z) {
    BLOCK_TYPE currentBlockType = m_blocks[x + 1][y + 1][z + 1].GetType();
    BLOCK_TYPE topBlockType = m_blocks[x + 1][y + 2][z + 1].GetType();

    if (!Block::IsOpaque(currentBlockType)) return false;  // 지면 블록이어야 함
    if (topBlockType != BLOCK_TYPE::BLOCK_AIR) return false;  // 위는 빈 공간

    return true;
}
```

**PlaceTree() - TreeShape → 월드 블록 변환**:
```cpp
void Chunk::PlaceTree(int x, int y, int z, ChunkLoadMemory* memory, TREE_TYPE treeType) {
    PosInt3 worldPosInt3 = VectorToPosInt3(m_offsetPosition + Vector3(x, y, z));

    // 1. TreeShape 생성 (25x25x25)
    TreeShape treeShape = { TREE_BLOCK_INDEX::EMPTY };
    Tree::GenerateTreeShape(treeType, worldPosInt3, treeShape);

    // 2. TreeShape → 월드 블록 변환
    for (int dy = 0; dy < Tree::TREE_SIZE; ++dy) {
        for (int dz = 0; dz < Tree::TREE_SIZE; ++dz) {
            for (int dx = 0; dx < Tree::TREE_SIZE; ++dx) {
                if (treeShape[dy][dz][dx] == EMPTY) continue;

                int ty = y + dy + 1;
                int tz = z - dz + (Tree::TREE_SIZE / 2);  // 중앙 정렬
                int tx = x + dx - (Tree::TREE_SIZE / 2);

                // TRUNK 또는 LEAF 블록
                if (treeShape[dy][dz][dx] == TRUNK || treeShape[dy][dz][dx] == LEAF) {
                    BLOCK_TYPE treeBlock = (treeShape[dy][dz][dx] == TRUNK)
                                           ? Tree::GetTrunkBlockType(treeType)
                                           : Tree::GetLeafBlockType(treeType);

                    SetTreeBlockType(tx, ty, tz, treeBlock, memory);
                }

                // VINE 플래그
                if (treeShape[dy][dz][dx] >= VINE) {
                    uint8_t faceFlag = (treeShape[dy][dz][dx] & (~VINE));  // VINE 플래그 제거
                    SetTreeVines(tx, ty, tz, INSTANCE_TYPE::INSTANCE_VINE, faceFlag, memory);
                }
            }
        }
    }
}
```

**SetTreeBlockType() - 블록 설정 + 패치 전파**:
```cpp
void Chunk::SetTreeBlockType(int tx, int ty, int tz, BLOCK_TYPE treeBlock, ChunkLoadMemory* memory) {
    // 1. 청크 내부 (padding 포함: -1 ~ 32)
    if (IsInsideChunkWithPadding(tx, ty, tz)) {
        m_blocks[tx + 1][ty + 1][tz + 1].SetType(treeBlock);
    }

    // 2. 청크 외부면 PatchData 생성
    if (!IsInsideChunk(tx, ty, tz)) {
        PatchData patchData = ChunkManager::GetInstance()->MakePatchData(
            tx, ty, tz, treeBlock, Instance(), CHUNK_SIZE, true);

        Vector3 blockPos = m_offsetPosition + Vector3(tx, ty, tz);
        Vector3 blockOwnerOffsetPos = Utils::CalcOffsetPos(blockPos, CHUNK_SIZE);
        PosInt3 blockOwnerOffsetPosInt3 = Utils::VectorToPosInt3(blockOwnerOffsetPos);

        memory->chunkPatchDataMap[blockOwnerOffsetPosInt3].insert(patchData);
    }

    // 3. Edge 블록이면 인접 청크 패치 전파 (Greedy Meshing 대비)
    if (IsInnerEdge(tx, ty, tz) || IsOuterEdge(tx, ty, tz)) {
        Vector3 blockPos = m_offsetPosition + Vector3(tx, ty, tz);
        Vector3 blockOwnerOffsetPos = Utils::CalcOffsetPos(blockPos, CHUNK_SIZE);

        int localX = Utils::WrapToBase(tx, CHUNK_SIZE);
        int localY = Utils::WrapToBase(ty, CHUNK_SIZE);
        int localZ = Utils::WrapToBase(tz, CHUNK_SIZE);

        std::pair<PosInt3, PatchData> outEdgePatchEntry[3];
        int outEdgePatchEntryCount = 0;
        ChunkManager::GetInstance()->GenerateEdgePatchEntry(localX, localY, localZ,
            blockOwnerOffsetPos, treeBlock, outEdgePatchEntry, outEdgePatchEntryCount);

        PosInt3 myOffsetPosInt3 = Utils::VectorToPosInt3(m_offsetPosition);
        for (int i = 0; i < outEdgePatchEntryCount; ++i) {
            PosInt3& patchChunkPosInt3 = outEdgePatchEntry[i].first;
            PatchData& patchData = outEdgePatchEntry[i].second;

            if (patchChunkPosInt3 != myOffsetPosInt3) {
                memory->chunkPatchDataMap[patchChunkPosInt3].insert(patchData);
            }
        }
    }
}
```

**핵심**:
- **IsInsideChunkWithPadding(-1~32)**: 청크 내부 + 1블록 padding 영역은 직접 설정
- **IsInsideChunk(0~31)**: 청크 외부는 PatchData로 인접 청크에 전달
- **IsInnerEdge/IsOuterEdge**: 청크 경계 블록은 Greedy Meshing 시 인접 청크 블록 참조하므로 추가 패치 전파

## 5. 문제점 & 해결

### 5.1. 청크 경계 나무 배치 문제

**문제**: 나무는 25x25x25 크기로 청크(32x32x32)를 넘을 수 있으나, 청크 로딩 순서가 비동기적이므로 나무가 먼저 생성된 후 인접 청크가 로드되면 충돌 발생.

**해결**: PatchData 시스템 사용
- 나무 블록이 청크 외부에 위치하면 `chunkPatchDataMap`에 패치 데이터 저장
- 인접 청크 로딩 시 `Chunk::Patch()`에서 패치 데이터 적용
- ChunkManager가 패치 의존성 추적하여 순서 보장

**트레이드오프**:
- 장점: 청크 경계를 넘는 대형 나무(Jungle, Cherry) 정상 생성
- 단점: 패치 데이터 메모리 오버헤드 (청크당 수십~수백 개 PatchData)

### 5.2. TreeShape 메모리 크기

**문제**: TreeShape는 `uint8_t[25][25][25]` = 15,625 바이트(약 15KB)로 스택 오버플로우 위험.

**해결**: 스택 할당
```cpp
TreeShape treeShape = { TREE_BLOCK_INDEX::EMPTY };  // 스택 할당
```
- 함수 스코프 내에서만 사용하므로 스택 할당 허용
- TREE_SIZE=25는 충분히 작아서 스택 오버플로우 발생 안 함 (일반적으로 1MB 스택 크기)

**트레이드오프**:
- 장점: 힙 할당/해제 오버헤드 없음, 빠른 생성
- 단점: 재귀 호출 시 스택 누적 위험 (현재는 재귀 없음)

### 5.3. 랜덤 방향 가지의 끊김 문제

**문제**: `AddBranchForRandom()`에서 랜덤 방향으로 가지 생성 시 TreeShape 경계(0~24)를 벗어나면 `continue`로 스킵하여 가지가 중간에 끊김.

**해결**: 경계 검사 + 조기 종료 없음
```cpp
if (nx < 0 || nx >= Tree::TREE_SIZE || ...) {
    continue;  // 블록 설정 스킵하지만 루프는 계속
}
```
- 경계 밖 블록은 설정하지 않지만 루프는 계속 진행
- `lastBranchPos`는 마지막 유효 블록 위치 기록
- 가지가 경계를 벗어나도 잎은 `lastBranchPos`에 배치되어 시각적 완성도 유지

**트레이드오ff**:
- 장점: 가지가 경계에 막혀도 최소한 잎은 생성
- 단점: 매우 긴 가지는 중간이 빈 공간 (현재 branchLength=4~13으로 문제 없음)

### 5.4. Ellipsoid 잎의 각진 형태

**문제**: `AddLeafCluster()`의 ellipsoid 공식(`sx*sx + sy*sy + sz*sz < r*r`)으로 생성한 잎이 블록 단위로 각져 보임.

**해결**: Carving + Shrink 조합
```cpp
// Carving: Y축 특정 범위에서 랜덤 불규칙
if (carveStartY <= y && y <= carveEndY) {
    uint32_t hash = HashInt(worldX * worldY * worldZ, 100043u);
    sx *= (1.0f + (carveScale * (hash % 2)));  // 0% or 12.5% 확장
    sz *= (1.0f + (carveScale * (hash % 2)));
}

// Shrink: Y축 늘림으로 타원 형태
Vector3 shrink = Vector3(1.0f, 2.0f, 1.0f);
float sy = y * shrink.y;
```

**결과**:
- Shrink: Y축 2배 늘림으로 수직 타원 형태
- Carving: 12.5% 확률로 X/Z 방향 확장하여 불규칙한 경계 생성
- 두 효과 조합으로 유기적 형태

**트레이드오프**:
- 장점: 자연스러운 잎 모양
- 단점: Carving이 월드 좌표 기반이므로 동일 나무 타입도 위치에 따라 형태 다름 (일부러 의도한 다양성)

### 5.5. 바이옴 경계의 나무 타입 불일치

**문제**: 청크 내 바이옴이 혼합된 경우(예: 50% Plains, 50% Forest), 나무 위치의 바이옴만 고려하여 인접 타일에 어색한 나무 배치 가능.

**해결**: 바이옴별 나무 개수 제한
```cpp
uint32_t maxTreeCountByRatio = GetMaxPlaceCountByBiomeRatio(
    biomeType, Biome::GetMaxTreeCountPerChunk(biomeType), memory->biomeCount[biomeType]);
```

**공식**:
```cpp
float biomeRatio = biomeCount / (float)CHUNK_SIZE2;  // 바이옴 비율
float placeCountByRatio = biomeRatio * (float)maxCountPerChunk;
```

**예시**:
- Plains 바이옴: maxCountPerChunk=5
- 청크 내 Plains 비율: 30% (307/1024)
- 실제 배치 가능 나무: 5 × 0.3 = 1~2그루

**결과**: 바이옴 비율에 따라 나무 밀도 조정하여 경계가 자연스러움

**트레이드오프**:
- 장점: 바이옴 전환 부드러움
- 단점: 작은 바이옴 패치(5% 미만)는 나무 0개 배치 가능

## 6. 결과

### 6.1. 8종 나무 형태 비교

| 나무 타입   | 높이 범위 | 특징                                      | 주요 바이옴          |
|-----------|----------|-------------------------------------------|---------------------|
| OAK       | 5~7      | 기본 타원형 잎, 상단 캡                      | Plains, Forest      |
| BIRCH     | 4~6      | 작고 좁은 타원형 잎                          | Forest, Seasonforest|
| SPRUCE    | 9~15     | 원뿔형 침엽수 (shrink.y=2.5)                | Taiga, Tundra       |
| MANGROVE  | 5~7      | 넓은 잎 + 덩굴 40%                          | Swamp               |
| CHERRY    | 7~9      | 가지 2개 + 큰 구형 잎 (leafRadius=6)        | Shrubland           |
| CACTUS    | 5~7      | 가지 2개, 잎 없음                           | Desert              |
| JUNGLE    | 16~20    | 2x2 trunk + 가지 3개 + 평면 잎 + 덩굴 5~10% | Rainforest          |
| ACACIA    | 8~10     | 가지 1개 + 평면 잎                          | Savanna             |

### 6.2. 바이옴별 나무 분포

**Forest 바이옴 예시**:
- Oak 80%, Birch 20% (distribution < 0.8)
- 청크당 평균 3~5그루 배치

**Swamp 바이옴 예시**:
- Oak 70%, Mangrove 30% (distribution < 0.3)
- 덩굴 40% 확률로 Mangrove에 집중

**Savanna 바이옴 예시**:
- Oak 30%, Acacia 70% (distribution < 0.3)
- Acacia의 특수한 평면 잎 구조로 사바나 분위기

### 6.3. 청크 간 일관성

- **패치 시스템**: 25x25x25 나무가 최대 7개 청크(3x3x3 영역)에 걸쳐도 정상 생성
- **Greedy Meshing 대응**: Edge 블록 패치로 청크 경계의 메쉬도 올바르게 생성
- **로딩 순서 독립**: 청크 A의 나무가 청크 B에 걸쳐도 B가 나중에 로드되면 패치 적용

## 7. 회고

### 7.1. 아쉬운 점

1. **TreeShape 크기 고정**:
   - 모든 나무가 25x25x25 고정 크기 사용
   - Birch(높이 4~6)는 낭비, Jungle(높이 16~20)은 부족
   - **개선 방향**: 나무 타입별 가변 크기 TreeShape 도입 (Oak: 15x15x15, Jungle: 30x30x30)

2. **가지 생성의 제한적 제어**:
   - Gradient 방식은 Greedy 알고리즘으로 직선에 가까움
   - 마인크래프트 Azalea의 구불구불한 가지, Dark Oak의 캐노피 구조 구현 어려움
   - **개선 방향**: Bezier Curve 기반 가지 생성 또는 L-System 도입

3. **잎 형태의 단조로움**:
   - Ellipsoid와 원형 평면 두 가지만 사용
   - 모든 나무가 대칭적 형태
   - **개선 방향**: Perlin Noise 기반 잎 밀도 맵으로 비대칭 잎 클러스터 생성

4. **바이옴별 나무 밀도 하드코딩**:
   - `Biome::GetMaxTreeCountPerChunk()`가 각 바이옴별 값을 하드코딩
   - 밀도 조정 시 코드 수정 필요
   - **개선 방향**: JSON 파일로 바이옴별 나무 밀도, 타입 비율 정의

5. **TreeShape → 블록 변환 비효율**:
   - 25x25x25 = 15,625번 루프 순회하며 EMPTY 체크
   - 실제로 TRUNK/LEAF는 100~500개 정도로 대부분 EMPTY
   - **개선 방향**: TreeShape를 sparse structure(vector<TreeBlock>)로 변경하여 유효 블록만 저장

### 7.2. 다음에 개선하고 싶은 방향

1. **다단계 가지 (Sub-Branch)**:
   - 현재는 trunk → branch 1단계만 가능
   - 큰 나무(Jungle, Oak)는 branch → sub-branch로 2~3단계 가지 구조 추가
   - 더 자연스러운 나무 형태

2. **뿌리 시스템**:
   - Mangrove는 공중 뿌리, Jungle은 버트레스 뿌리(buttress roots) 특징
   - TreeShape의 Y < 0 영역도 사용하여 지면 아래 뿌리 생성
   - 지형과 상호작용 (경사면에서 뿌리 노출)

3. **계절별 나무 변화**:
   - Seasonforest 바이옴에서 계절(온도 변화)에 따라 잎 블록 타입 변경
   - 봄: 연두색 잎, 가을: 단풍 잎
   - Time 시스템과 연계하여 동적 변화

4. **나무 성장 애니메이션**:
   - 현재는 즉시 완성된 나무 생성
   - Chunk::Update()에서 나무가 서서히 위로 자라는 애니메이션 추가
   - TreeShape를 Y축 단계별로 적용하여 성장 시뮬레이션

5. **LOD (Level of Detail)**:
   - 멀리 있는 나무는 단순화된 모델 사용
   - TreeShape 생성 시 LOD 레벨별로 해상도 조정
   - LOD 0: 25x25x25 full detail, LOD 1: 15x15x15, LOD 2: 단일 블록
