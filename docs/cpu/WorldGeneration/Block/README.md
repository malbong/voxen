# Block

<img width="1571" height="778" alt="Image" src="https://github.com/user-attachments/assets/64ab434b-dafb-43aa-ab2d-69f381de182f" />

## 1. 개요

Block은 Voxel 월드를 구성하는 작은 단위이다. 이 블록 타입은 Chunk가 Load될 시에 `m_blocks[32][32][32]` 타입을 결정한다.

타입이 결정된 블록들은 단순히 `Block::m_type`만 결정되고, 텍스쳐 인덱스나 반투명/투명 등 정보는 정적 테이블에서 조회한다.

청크 내부에서 블록 타입이 모두 결정되면 메쉬를 생성하게 된다.

## 2. 실행 파이프라인에서의 위치

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

## 3. Block 구조 — Block → BlockTypeInfoSet → BlockTypeInfo

Block 도 [Biome](../Biome/README.md)과 같은 패턴을 쓴다. 즉

- `Block` — **정적 함수만 노출하는 접근 계층**, 개별 인스턴스는 `BLOCK_TYPE`(1byte) 하나의 값만을 가지고 있다.
- `BlockTypeInfoSet` — 256개(실사용 약 50 언저리)의 `BlockTypeInfo` 를 담는 컨테이너, 생성자에서 각 타입의 속성 세팅
- `BlockTypeInfo` — 블록의 정보를 담는 객체: 텍스처 인덱스(top/side/bottom) + 렌더링 카테고리(tp / op / sa) 세 개의 bool

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

`Block::IsOpaque(type)` 등은 모두 아래처럼 테이블 조회 위임한다.

```cpp
// voxen/srcs/Block.cpp
BlockTypeInfoSet Block::m_blockTypeInfoSet;

bool Block::IsOpaque(BLOCK_TYPE type)
{
    return m_blockTypeInfoSet.GetInfo(type).IsOpaque();
}
```

### 3.1 왜 OOP 를 쓰지 않았는가

[Biome](../Biome/README.md)과 동일

`GrassBlock : Block`, `StoneBlock : Block` … 처럼 상속으로 각 블록 타입을 표현하는 방법도 물론 가능하지만,
**정보를 담는 객체를 컨테이너에 넣는 방식**을 택했다.

- Chunk가 가지고 있는 Block의 개수가 많다.
  - `34x34x34`(패딩포함)개를 Chunk가 8bit로 타입만을 가지고 있다.
  - 인스턴스를 담거나 포인터를 담기에 비용은 최소 4배는 들어간다.
  - cf. 물론, 경량화 패턴으로 할 수 있을 것 같다.
- 다형성으로 구현하기에 로직이 크게 다르지 않았다
  - 블록을 이용한 상호작용이 많지 않았고, 블록이 다른건 렌더링과 단순히 상하좌우 등의 TextureIndex 정도만 상이했다.
  - 즉, 로직은 동일하되 성질만 다른 상황이였다. 이 때, OOP로 각 개별코드를 작성하기엔 부담이였다.
  - cf. 물론, 다른 블록의 로직이 존재할 수 있는 클라이언트 코드가 존재하지 않아 가능한 것이다.

### 3.2 렌더링 카테고리 세 종류

| 카테고리         | 예시                                         | 렌더링 처리                               |
| ---------------- | -------------------------------------------- | ----------------------------------------- |
| `isTransparency` | `AIR`, `WATER`                               | 물은 forward transparency pass 에서 그림  |
| `isOpaque`       | `GRASS`, `DIRT`, `STONE`, `SAND`, `OAK_LOG`… | G-Buffer opaque pass, greedy meshing 적용 |
| `isSemiAlpha`    | 모든 leaves (`OAK_LEAF`, `SPRUCE_LEAF`, …)   | opaque 이후 별도 semi-alpha pass          |

`GreedyMeshing` 은 같은 카테고리 + 같은 타입 블록만 합쳐 하나의 quad 로 만들 수 있으므로 카테고리 분리가 필요했다.

또한 메쉬 성격뿐만 아니라, 렌더링 파이프라인이 다르기에 구분할 필요가 존재했다.

### 3.3 BlockTypeInfo 내용

```cpp
class BlockTypeInfo {
    ...
private:
	uint8_t m_texTopIndex;      // 윗면 텍스쳐 인덱스 -> 아틀라스로 조회
	uint8_t m_texSideIndex;     // 옆면 텍스쳐 인덱스
	uint8_t m_texBottomIndex;   // 아랫면 텍스쳐 인덱스
	bool m_isTransparency;      // 블록의 성격이 투명인지
	bool m_isOpaque;            // 블록의 성격이 불투명인지
	bool m_isSemiAlpha;         // 블록의 성격이 투명도 불투명도 아닌지
};
```

위의 `BlockTypeInfo`를 Block type 마다 초기화해두고 그것을 `BlockTypeInfoSet` 컨테이너에 저장해서 static으로 사용한다.

## 4. Block 결정 방식

지형 생성의 최종 단계는 `Chunk::InitBasicBlockType` 이 `CHUNK_SIZE_P³` 를 순회하며 각 좌표에 대해 `Block::GetBlockType(...)` 를 호출하는 것이다.

```cpp
// voxen/srcs/Chunk.cpp
void Chunk::InitBasicBlockType(ChunkLoadMemory* memory)
{
    for (int x = 0; x < CHUNK_SIZE_P; ++x)
    for (int y = 0; y < CHUNK_SIZE_P; ++y)
    for (int z = 0; z < CHUNK_SIZE_P; ++z) {
        int worldX = (int)m_offsetPosition.x + x - 1; // -1: padding
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

### 4.1 `Block::GetBlockType` 의 결정 순서

```cpp
// voxen/srcs/Block.cpp
BLOCK_TYPE Block::GetBlockType(int x, int y, int z,
    float c, float e, float pv, float t, float h, float d, float elevation, BIOME_TYPE biomeType)
{
    // [1] 절대 바닥층
    if (y == Terrain::MIN_HEIGHT_LEVEL)                    // y == 0
        return BLOCK_BEDROCK;

    // [2] 공기 / 물 기본값 결정
    BLOCK_TYPE blockType = (y <= Terrain::WATER_HEIGHT_LEVEL)   // y <= 63
                              ? BLOCK_WATER : BLOCK_AIR;

    // [3] 수면에서 온도 비교 후 수면 얼음 처리
    if (y == Terrain::WATER_HEIGHT_LEVEL && t < 0.25f)
        blockType = BLOCK_ICE;

    // [4] 지표 아래이면서 동굴 아닐 시 월드 블록 구성
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

핵심은 `[4]`번이다.

<img width="556" height="547" alt="Image" src="https://github.com/user-attachments/assets/8230aa1f-12dc-4b21-8252-1f6593566fc9" />

기본적으로 AIR와 WATER를 초기화 시켜두고, `y < elevation` 기준으로 실제 월드를 구성할 타입을 결정한다.

`biomeLayer`는 BIOME 별로 다른 블록 타입을 결정할 때 사용하는 Layer 변수인데, 기본 표면 층의 높이다.

- 사용자가 직접 눈으로 확인하는 경우는 일반적으로 표면이 되고, 이는 표면에서의 타입이 Biome 별로 구별되어야 한다는 것이다.
- `biomeLayer`는 `e`와 `pv` 값을 이용하여 변주를 준다.
  - 반비례: 높으면 biomeLayer가 줄어듦

### 4.2 Cave 구성

<img width="1879" height="934" alt="Image" src="https://github.com/user-attachments/assets/fff2c29a-080d-48de-85cc-4a55e2e7a911" />

Cave는 단순히 3D 노이즈를 사용하여 임계값으로 비교(`threshold > d`)하여 조건을 만족하지 못하면 빈 블록으로 구성하게 끔 만들었었다.

- 하지만, 너무 동그란 형태의 동굴만 나오게 되었고, 그로 인해 수정할 필요가 존재했다.

다음으로 진행한 방식은 `[-threshold, threshold]`사잇값을 사용하는 것이였다.

- 이 방법은 동그란 형태의 동굴보다는 괜찮은 결과였지만, 오렌지껍질을 돌아다니는 형태의 동굴이라 마음에 들지 않았다.

그래서, 구글링 결과 Redit에서 오랜지껍질을 두 개 섞는 상상을 해보라는 글이 존재했다.

- 그래서 3D 노이즈를 두 개 사용하여 사잇값을 이용하니 결과는 괜찮아서 사용한다.
- 물론 3D 노이즈를 두 개 사용하니 비용에 부담은 당연히 존재한다.

```cpp
static bool IsCave(int x, int y, int z)
{
	float threshold = 0.004f;

	float density1 = Utils::PerlinFbm(x / 256.0f, y / 256.0f, z / 256.0f, 2.0f, 4);
	if (density1 * density1 > threshold) // 껍데기 형태: 제곱으로 threshold 사이값만 통과할 수 있음
		return false; // ealry return

	float density2 = Utils::PerlinFbm(
		x / 512.0f + 123.0f, y / 256.0f + 123.0f, z / 512.0f + 123.0f, 2.0f, 4);
	if (density2 * density2 > threshold) // 한번 더
		return false;

	return (density1 * density1 + density2 * density2 <= threshold);
}
```

### 4.3 Inner — density 노이즈 기반 (좌표 종속)

지표에서 biomeLayer 이상 깊어진 순간부터는 biome 과 무관하게 좌표 기준 노이즈 값으로 블록 타입을 결정한다.
이 때, Density 3D 노이즈를 사용하여 그 값에 따라 구분하여 타입을 결정한다.

```cpp
// voxen/srcs/Block.cpp
BLOCK_TYPE Block::GetBlockTypeForInner(int x, int y, int z, float distribution)
{
    float density = Terrain::GetDensity(x, y, z);           // [0, 1]

    if      (density <= 0.30f)  return BLOCK_DIRT;          // 얇은 흙 광맥
    else if (density <= 0.75f)  return BLOCK_STONE;         // 기본 돌
    else if (density <= 0.88f)  return BLOCK_ANDESITE;      // 안산암
    else {
        // 광석 — distribution 노이즈
        if      (distribution <= 0.4f)  return BLOCK_COAL_ORE;
        else if (distribution <= 0.6f)  return BLOCK_COPPER_ORE;
        else if (distribution <= 0.7f)  return BLOCK_IRON_ORE;
        else if (distribution <= 0.8f)  return BLOCK_REDSTONE_ORE;
        else if (distribution <= 0.9f)  return BLOCK_GOLD_ORE;
        else                            return BLOCK_DIAMOND_ORE;
    }
}
```

### 4.4 Biome — biome + y (표면과의 거리) 에 따라 결정

biomeLayer 두께 내부에서는 `GetBlockTypeForBiome(biome, y, elevation, d)` 가 호출된다.
biome 별로 switch case 로 분기하고(OOP가 나았을까?), 각 case 안에서는 `y == baseHeight` (표면 바로 그 칸) 인지 아닌지, 그리고 distribution 값을 판단하거나 여러 가지 상태에 따라 최종 블록을 정한다.

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

이러한 Biome 별 구분은 직접 렌더링 결과를 판단하면서 얻은 결과이며 하드하게 작성한 것들이다.

biome 별 대표 결과 요약:

| biome           | 표면 (y == baseHeight)                      | 표면 아래 (biomeLayer 안)  |
| --------------- | ------------------------------------------- | -------------------------- |
| OCEAN           | d 에 따라 구분: sand / dirt / gravel / clay | 동일                       |
| DESERT          | d 에 따라 구분:sand / sandstone             | 동일                       |
| TUNDRA          | d 에 따라 구분:ice / snow / snow-grass      | snow-grass / dirt / coarse |
| SNOWY_TAIGA     | snow-grass / grass / coarse                 | coarse / dirt              |
| TAIGA           | grass / podzol / coarse                     | coarse / dirt              |
| SWAMP           | mud / moss / grass / dirt                   | stone / moss / mud         |
| PLAINS 계열 6종 | grass (95%) / dirt (5%)                     | dirt (일부 stone)          |

- **PLAINS 계열 6개 biome (`PLAINS`, `FOREST`, `SHRUBLAND`, `RAINFOREST`,
  `SEASONFOREST`, `SAVANNA`)** 은 표면층 로직이 같다. 서로 다르게 보이는 이유는
  **표면 블록 위에 얹히는 트리 / 인스턴스가 다르기 때문** (다른 biome 파라미터 → 다른 트리·꽃 후보 목록).
- OCEAN, DESERT 는 표면과 아래층 구분이 없다.
- TUNDRA 만 표면 / 표면-1 / 그 아래 3층을 구분해 눈이 흙 위에만 얇게 얹히는 표현을 낸다.

## 5. 회고

**정적 테이블 + 8-bit 타입 코드** 조합이 이 프로젝트 구조에서 가장 결정적으로 잘한 선택인 것 같았지만 돌이켜보니 경량화패턴으로 구현이 가능해보인다.

- 개선 코드 구조
  - Block을 상속받는 BlockA, B, C를 OOP로 구성한다.
  - Chunk가 인스턴스를 직접 가지고 있거나, 포인터를 가질 필요 없이 타입만 그대로 가지고 있는다.
  - BlockA, B, C는 정적으로 하나만 초기화 시켜둔다.
  - 현재 코드와 유사하게 인터페이스를 통해 값을 참조한다.
- 이 방법으로 코드는 OOP로 분리하고, 메모리 부담없이 사용할 수 있을 것 같다.

**아쉬운 점**: `GetBlockTypeForBiome` 이 switch/case 로 12개 biome 을 나열하고 있다.

- biome 추가 시 두 곳 (biome 등록 + block 로직) 을 동시에 손대야 한다.
- Biome을 추가했는데 GetBlockTypeForBiome에 들어가서 값을 추가해야되는 커플링이 존재한다.
- 이 점은 위와 동일하게 OOP로도 충분히 해결할 수 있을 것 같다.
