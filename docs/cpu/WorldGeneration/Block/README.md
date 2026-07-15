# Block System

Voxel 세계의 최소 단위. Voxen 은 약 50여 종의 블록 타입을 정의하고,
지형 생성 단계에서 각 좌표에 대해 `AIR` → `WATER` → `Inner (density 기반 광물/돌)`
→ `Biome (표면층)` 순으로 우선순위를 뒤집으며 하나의 블록 타입을 결정한다.
결정된 타입은 `Block::m_type` 한 필드에만 저장되며, 렌더링에 필요한
텍스처 인덱스나 반투명/불투명 여부는 모두 전역 정적 테이블에서 조회한다.

## 1. 개요

### 1.1 자료구조 — Block → BlockTypeInfoSet → BlockTypeInfo

Block 도 [Biome](../Biome/README.md) 과 같은 패턴을 쓴다. 즉

- `Block` — **정적 함수만 노출하는 접근 계층 (facade)** 이자
  개별 인스턴스가 `BLOCK_TYPE` 한 값만 들고 있는 얇은 값 객체
- `BlockTypeInfoSet` — 256개 (`BLOCK_TYPE_COUNT`) `BlockTypeInfo` 를
  vector 로 담는 컨테이너, 생성자에서 각 타입의 속성 세팅
- `BlockTypeInfo` — 텍스처 인덱스(top/side/bottom) + 렌더링 카테고리
  (transparency / opaque / semiAlpha) 세 개의 bool

```cpp
// voxen/headers/Block.h  (요약)
class Block {
public:
    static const uint32_t BLOCK_TYPE_COUNT = 256;

    static bool       IsTransparency        (BLOCK_TYPE);
    static bool       IsOpaque              (BLOCK_TYPE);
    static bool       IsSemiAlpha           (BLOCK_TYPE);
    static uint8_t    GetBlockTextureIndex  (BLOCK_TYPE, uint8_t face);
    static BLOCK_TYPE GetBlockTypeForInner  (int x, int y, int z, float distribution);
    static BLOCK_TYPE GetBlockTypeForBiome  (BIOME_TYPE, int y, float h, float d);
    static BLOCK_TYPE GetBlockType(int x, int y, int z,
        float c, float e, float pv, float t, float h, float d, float elevation,
        BIOME_TYPE);

    Block() : m_type(BLOCK_NONE) {}
    Block(BLOCK_TYPE type) : m_type(type) {}

    inline BLOCK_TYPE GetType()          const { return m_type; }
    inline void       SetType(BLOCK_TYPE t)    { m_type = t; }

private:
    static BlockTypeInfoSet m_blockTypeInfoSet;   // 전역 정적 테이블
    BLOCK_TYPE              m_type;               // 인스턴스가 가진 유일한 상태
};
```

`Block::IsOpaque(type)` 등은 모두 아래처럼 정적 테이블 조회로 위임된다.

```cpp
// voxen/srcs/Block.cpp
BlockTypeInfoSet Block::m_blockTypeInfoSet;

bool Block::IsOpaque(BLOCK_TYPE type)
{
    return m_blockTypeInfoSet.GetInfo(type).IsOpaque();
}
```

### 1.2 왜 OOP 를 쓰지 않았는가

`GrassBlock : Block`, `StoneBlock : Block` … 처럼 상속으로 각 블록 타입을 표현하는
방법도 가능하지만, Voxen 은 아래 두 이유로 **정적 테이블 + 값 객체** 조합을 채택했다.

- **인스턴스 개수가 극단적으로 많다.** 청크 하나가 32³ = 32,768 블록이고
  최대 CHUNK_POOL_SIZE (수백 개) 만큼의 청크가 상주한다.
  개별 블록마다 vtable 포인터(8 byte) 를 붙이면 청크당 256 KB, 전체로는 수십 MB 의
  오버헤드가 발생한다.
  현재 구조에서는 블록 하나가 `BLOCK_TYPE (uint8_t)` 하나만 가지므로 청크의
  블록 배열은 CHUNK_SIZE_P³ × 1 byte = **약 39 KB** 로 압축된다.
- **정적 속성은 타입 하나당 딱 한 세트만 있으면 된다.** 텍스처 인덱스, 렌더링
  카테고리는 좌표 / 인스턴스와 무관하게 타입에만 종속되므로 전역 테이블 한 벌이면 충분하다.

즉 **"블록의 정적 속성은 정적 테이블에, 좌표별 상태는 8-bit 타입 코드에"** 라는
분리 구조를 택한 것이 이 자료구조의 핵심이다.

### 1.3 렌더링 카테고리 세 종류

각 블록 타입은 `Init(topTex, sideTex, bottomTex, isTransparency, isOpaque, isSemiAlpha)`
로 초기화된다. 세 bool 은 상호 배타적으로 **렌더링 파이프라인의 어느 슬롯에 들어가는지**
를 결정한다.

| 카테고리         | 예시                                          | 렌더링 처리                               |
| ---------------- | --------------------------------------------- | ----------------------------------------- |
| `isTransparency` | `AIR`, `WATER`                                | 물은 forward transparency pass 에서 그림  |
| `isOpaque`       | `GRASS`, `DIRT`, `STONE`, `SAND`, `OAK_LOG`… | G-Buffer opaque pass, greedy meshing 적용 |
| `isSemiAlpha`    | 모든 leaves (`OAK_LEAF`, `SPRUCE_LEAF`, …)   | opaque 이후 별도 semi-alpha pass          |

`GreedyMeshing` 은 같은 카테고리 + 같은 타입 블록만 합쳐 하나의 quad 로 만들 수 있으므로
카테고리 분리가 곧 mesh bucket 분리이기도 하다.
이 결정은 `Chunk` 가 opaque / transparency / semiAlpha vertex buffer 를 각각 따로
관리하는 이유이기도 하다.

## 2. Block 결정 방식

지형 생성의 최종 단계는 `Chunk::InitBasicBlockType` 이 `CHUNK_SIZE_P³` 를 순회하며
각 좌표에 대해 `Block::GetBlockType(...)` 를 호출하는 것이다.

```cpp
// voxen/srcs/Chunk.cpp
void Chunk::InitBasicBlockType(ChunkLoadMemory* memory)
{
    for (int x = 0; x < CHUNK_SIZE_P; ++x)
    for (int y = 0; y < CHUNK_SIZE_P; ++y)
    for (int z = 0; z < CHUNK_SIZE_P; ++z) {
        int worldX = (int)m_offsetPosition.x + x - 1;
        int worldY = (int)m_offsetPosition.y + y - 1;
        int worldZ = (int)m_offsetPosition.z + z - 1;

        BLOCK_TYPE blockType = Block::GetBlockType(worldX, worldY, worldZ,
            memory->continentalinessNoises[x][z], memory->erosionNoises   [x][z],
            memory->peaksValleyNoises  [x][z],   memory->temperatureNoises[x][z],
            memory->humidityNoises     [x][z],   memory->distributionNoises[x][z],
            memory->elevationNoises    [x][z],   memory->biomeMap2D       [x][z]);

        m_blocks[x][y][z].SetType(blockType);
    }
}
```

노이즈 (`c/e/pv/t/h/d/elevation`) 는 xz 2D 만 캐싱돼 있고 y 축 순회는 **캐시 재사용**이라
비용이 저렴하다 ([Terrain 문서](../Terrain/README.md) 참고).

### 2.1 `Block::GetBlockType` 의 결정 순서

```cpp
// voxen/srcs/Block.cpp
BLOCK_TYPE Block::GetBlockType(int x, int y, int z,
    float c, float e, float pv, float t, float h, float d, float elevation,
    BIOME_TYPE biomeType)
{
    // [0] 테스트 영역 (개발용) — 상수 좌표에서만 유효, 배포 시 무시
    if (0 <= x && x < 400 && 0 <= z && z < 256 && 100 <= y && y <= 102)
        return GetTestBlocks(x, y, z);

    // [1] 절대 바닥층
    if (y == Terrain::MIN_HEIGHT_LEVEL)                    // y == 0
        return BLOCK_BEDROCK;

    // [2] 공기 / 물 기본값 결정
    BLOCK_TYPE blockType = (y <= Terrain::WATER_HEIGHT_LEVEL)   // y <= 63
                              ? BLOCK_WATER : BLOCK_AIR;

    // [3] 수면 얼음 처리 — 수면 위 한 칸이 매우 추운 지역이면 얼음
    if (y == Terrain::WATER_HEIGHT_LEVEL && t < 0.25f)
        blockType = BLOCK_ICE;

    // [4] 지표 아래 & 동굴 아님 → 내부 / 표면층 분기
    if (y < elevation && !Terrain::IsCave(x, y, z)) {
        int biomeLayer =
            2 + (int)(6.0f * (1.0f - e) * powf(((-pv + 1.0f) * 0.5f), 0.5f));

        if (y <= elevation - biomeLayer)                   // 깊숙한 내부
            blockType = GetBlockTypeForInner(x, y, z, d);
        else                                               // 표면 근처 (biomeLayer 두께)
            blockType = GetBlockTypeForBiome(biomeType, y, elevation, d);
    }

    return blockType;
}
```

핵심은 **"공기 / 물 → Inner → Biome" 세 층이 겹쳐 있고, 뒤에 오는 층이 앞의 결과를
덮어쓴다"** 는 구조다. 아래 그림처럼 y 축을 따라 결정 경로가 갈린다.

```
world Y                                    결정 규칙
─────────────────────────────────────────────────────────────
  y = 255  ┐
     …     │  y > elevation                                → BLOCK_AIR  (또는 y ≤ 63 이면 WATER, 아주 추우면 ICE 한 칸)
  y = elev ┤
     ↑     │
biomeLayer │  elevation - biomeLayer < y ≤ elevation       → GetBlockTypeForBiome  (표면층: grass, sand, snow …)
     ↓     │
  y = ...  ┤
     …     │  y ≤ elevation - biomeLayer  &&  !IsCave      → GetBlockTypeForInner  (density 기반: dirt / stone / andesite / ore)
     …     │  y ≤ elevation - biomeLayer  &&  IsCave       → BLOCK_AIR  (덮어쓰이지 않음: 기본값이 AIR/WATER 로 남음)
  y = 0    ┘  y == 0                                        → BLOCK_BEDROCK
```

- 표면(**biomeLayer**) 은 침식이 강할수록 얇고(`(1-e)` 작음), 험한 지형일수록 두꺼워진다
  (`pv` 가 -1 에 가까울수록 `((-pv+1)/2)^0.5` 커짐). 최소 2 블록.
- **동굴은 별도 mesh 를 만들지 않고**, 지표 아래에서 `IsCave` 가 true 인 좌표를 그냥
  기본값(AIR / WATER) 으로 두어 자연스럽게 파인 공간이 된다.

### 2.2 Inner — density 노이즈 기반 (좌표 종속)

지표에서 biomeLayer 이상 깊어진 순간부터는 biome 과 무관하게 **오직 좌표만으로**
결정된다. `Terrain::GetDensity(x, y, z)` (3D Perlin FBM, scale 16, freq 2, oct 2) 를 뽑고
그 값에 따라 계층적으로 분기한다.

```cpp
// voxen/srcs/Block.cpp
BLOCK_TYPE Block::GetBlockTypeForInner(int x, int y, int z, float distribution)
{
    float density = Terrain::GetDensity(x, y, z);           // [0, 1]

    if      (density <= 0.30f)  return BLOCK_DIRT;          // 얇은 흙 광맥
    else if (density <= 0.75f)  return BLOCK_STONE;         // 기본 돌
    else if (density <= 0.88f)  return BLOCK_ANDESITE;      // 안산암
    else {
        // 광석 — distribution 노이즈 (scale 24, 좌표별 hash 같은 값) 로 결정
        if      (distribution <= 0.4f)  return BLOCK_COAL_ORE;
        else if (distribution <= 0.6f)  return BLOCK_COPPER_ORE;
        else if (distribution <= 0.7f)  return BLOCK_IRON_ORE;
        else if (distribution <= 0.8f)  return BLOCK_REDSTONE_ORE;
        else if (distribution <= 0.9f)  return BLOCK_GOLD_ORE;
        else                            return BLOCK_DIAMOND_ORE;
    }
}
```

- **density 로 큰 덩어리 (돌 / 안산암 / 광맥) 를 분리** 하고 그 안에서 **distribution
  으로 광종을 결정** 하는 2단 구조. 두 노이즈는 seed 가 완전히 다르므로 광석 분포와
  density 덩어리가 무상관하게 섞인다.
- 광석은 전체 density 의 12% (>0.88 영역) 내에서만 생기고, 그 안에서도 흔한 것
  (COAL 40%) 부터 희귀한 것 (DIAMOND 10%) 까지 확률 차등이 있다.

### 2.3 Biome — biome + y (표면과의 거리) 에 따라 결정

biomeLayer 두께 내부에서는 `GetBlockTypeForBiome(biome, y, elevation, d)` 가 호출된다.
biome 별로 switch case 로 분기하고, 각 case 안에서는 `y == baseHeight` (표면
바로 그 칸) 인지 아닌지, 그리고 distribution 값에 따라 최종 블록을 정한다.

```cpp
case BIOME_PLAINS:
case BIOME_FOREST:
case BIOME_SHRUBLAND:
case BIOME_RAINFOREST:
case BIOME_SEASONFOREST:
case BIOME_SAVANNA:
    if (y == baseHeight) {                          // 최상단 = 표면
        if (d <= 0.95f)  return BLOCK_GRASS;
        else             return BLOCK_DIRT;         // 5% 확률로 잔디가 벗겨진 흙 노출
    }
    else if (y == baseHeight - 1) {                 // 표면 바로 아래 한 칸
        if (d <= 0.2f)   return BLOCK_STONE;
        else             return BLOCK_DIRT;
    }
    else {                                          // biomeLayer 안 그 아래
        return BLOCK_DIRT;
    }
```

biome 별 대표 결과 요약:

| biome            | 표면 (y == baseHeight)                  | 표면 아래 (biomeLayer 안) |
| ---------------- | --------------------------------------- | ------------------------- |
| OCEAN            | d 에 따라 sand / dirt / gravel / clay   | 동일                      |
| DESERT           | d 에 따라 sand / sandstone              | 동일                      |
| TUNDRA           | d 에 따라 ice / snow / snow-grass       | snow-grass / dirt / coarse |
| SNOWY_TAIGA      | snow-grass / grass / coarse             | coarse / dirt             |
| TAIGA            | grass / podzol / coarse                 | coarse / dirt             |
| SWAMP            | mud / moss / grass / dirt               | stone / moss / mud        |
| PLAINS 계열 6종  | grass (95%) / dirt (5%)                 | dirt (일부 stone)         |

- **PLAINS 계열 6개 biome (`PLAINS`, `FOREST`, `SHRUBLAND`, `RAINFOREST`,
  `SEASONFOREST`, `SAVANNA`)** 은 표면층 로직이 같다. 서로 다르게 보이는 이유는
  **표면 블록 위에 얹히는 트리 / 인스턴스가 다르기 때문** (다른 biome 파라미터 →
  다른 트리·꽃 후보 목록).
- OCEAN, DESERT 는 표면과 아래층 구분이 없다.
- TUNDRA 만 표면 / 표면-1 / 그 아래 3층을 구분해 눈이 흙 위에만 얇게 얹히는 표현을 낸다.

### 2.4 결정 파라미터 요약

| 축                                    | 결정 요소                                    |
| :------------------------------------ | :------------------------------------------- |
| **y** vs `MIN_HEIGHT_LEVEL(0)`        | 절대 바닥 → BEDROCK                          |
| **y** vs `WATER_HEIGHT_LEVEL(63)`     | 기본값 AIR 또는 WATER                        |
| **temperature** at y = WATER_HEIGHT   | 수면 얼음 여부 (`t < 0.25` 시 ICE 한 칸)     |
| **y** vs `elevation`                  | 지상/공중인지 여부                           |
| **`IsCave(x,y,z)`**                   | 지하에서 공기 공간 뚫음                      |
| **erosion, peaksValley**              | biomeLayer 두께                              |
| **density** (3D noise, Inner 전용)    | 흙/돌/안산암/광맥 큰 덩어리 결정             |
| **distribution** (2D noise)           | 광종 결정 (Inner), 표면 variant 결정 (Biome) |
| **biomeType**                         | 표면층 재질 계열 결정 (Biome)                |
| **y** vs `baseHeight` (Biome 내부)    | 표면인지, 표면-1 인지, 그 아래인지           |

## 3. 실행 파이프라인에서의 위치

`Block::GetBlockType` 은 `Chunk` 로드 파이프라인의 세 번째 단계다.

```
Chunk::Initialize
├─ InitTerrainNoises        ── c/e/pv/t/h/d/elevation 캐싱  (2D)
├─ InitBiomeMapAndCount     ── 각 (x,z) 의 BIOME_TYPE 결정  (2D)
├─ InitBasicBlockType       ── 각 (x,y,z) 의 BLOCK_TYPE 결정  ← 여기
├─ InitTreePlace            ── biomeType → 트리 종류/위치, 결과는 Block 을 덮어씀
├─ InitInstancePlace        ── biomeType → 풀/꽃 인스턴스 배치
└─ InitWorldVerticesData    ── m_blocks 배열을 greedy meshing 으로 mesh 화
```

- `InitBasicBlockType` 이 끝난 시점에서는 아직 트리가 심어져 있지 않고, 지형 기본만 채워진 상태다.
- 이후 `InitTreePlace` 는 이미 결정된 표면 블록 위에 `BLOCK_OAK_LOG` 등을 덮어쓴다.
  트리 블록은 `BlockTypeInfoSet` 에 이미 등록돼 있으므로 별도 처리 없이 opaque 로 렌더링된다.
- Leaves (`isSemiAlpha`) 는 같은 mesh 버킷에 속하지 않고 semi-alpha vertex buffer 에 별도로 쌓인다.

## 4. 참고 — `GetTestBlocks` 개발용 오버라이드

`GetBlockType` 의 첫 분기 `0 <= x && x < 400 && 0 <= z && z < 256 && 100 <= y && y <= 102`
구간은 **개발 중 텍스처/렌더링 확인용** 이다. 이 영역에는 지형 로직을 우회하고
모든 광석/원목/재질을 8 블록 폭의 스트라이프 패턴으로 강제 배치한다.
포트폴리오 시연 시 이 코드 블록은 남겨두거나 빌드 매크로로 감싸는 편이 안전하다.

## 5. 회고

- **정적 테이블 + 8-bit 타입 코드** 조합이 이 프로젝트 구조에서 가장 결정적으로 잘한 선택이었다.
  블록 배열이 39 KB (uint8) vs 314 KB (BlockTypeInfo 값 embed) 로 8배 차이가 나고,
  이 차이가 청크 풀 크기 × 청크 개수만큼 곱해지기 때문에 렌더링 성능뿐 아니라 메모리에도 크다.

- **결정 규칙이 순차적 override 방식**이라 읽기는 쉽지만, 새로운 조건 (예:
  "화산 근처는 지표에 obsidian") 을 넣을 때 어느 분기에 끼워 넣을지 매번 고민해야 한다.
  나중에는 순서 있는 규칙 리스트 (`std::vector<Rule>`) 로 분리해서 우선순위 명시적으로 관리하는
  방법도 고려해봄직하다.

- **아쉬운 점** : `GetBlockTypeForBiome` 이 switch/case 로 12개 biome 을 나열하고 있어
  biome 추가 시 두 곳 (biome 등록 + block 로직) 을 동시에 손대야 한다.
  Biome 마다 `BlockLayerPolicy` 함수를 하나 갖게 만들어 데이터 주도로 옮기면 새 biome 추가가
  한 파일에서 완결된다.

- **개선하고 싶은 방향** : Inner 광석 확률이 현재 하드코드 (0.4 / 0.6 / 0.7 / 0.8 / 0.9) 로
  y 축과 무관하다. Minecraft 처럼 y 깊이에 따라 다이아몬드는 아래, 석탄은 위쪽 같이
  확률 분포를 y 종속으로 만들면 훨씬 자연스럽다.
  `GetBlockTypeForInner` 시그니처에 이미 `y` 가 있어 확장 비용이 낮다.
