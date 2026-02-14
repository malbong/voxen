## 1. 개요

- **블록 타입 시스템(Block Type System)**은 월드를 구성하는 모든 블록의 속성과 배치 규칙을 관리하는 시스템
- 256개의 블록 타입을 지원하며, 각 블록은 텍스처 인덱스, 투명도 속성, 생성 규칙을 가짐
- 3단계 블록 선택 로직을 통해 높이, 바이옴, 밀도, 분포에 따라 적절한 블록 타입을 결정
- 프로젝트 전체에서 **월드 생성(World Generation)의 최종 단계**로, 실제 월드의 시각적 다양성을 담당

## 2. 도입 동기

- **블록 속성 중앙 관리**: 256개 블록 타입의 텍스처, 투명도 속성을 일관되게 관리
- **렌더링 최적화**: 투명도 플래그(Transparency, Opaque, SemiAlpha)로 렌더링 패스 분리
- **바이옴별 다양성**: 동일한 바이옴 내에서도 분포(distribution)에 따라 다양한 표면 블록 생성
- **현실적인 광석 분포**: 밀도(density) 기반으로 희귀 광석을 자연스럽게 배치
- **텍스처 아틀라스 최적화**: 각 블록의 면(top/side/bottom)별로 다른 텍스처 인덱스 할당

## 3. 핵심 아이디어

### 3.1 BlockTypeInfoSet - 중앙 집중식 블록 정보 관리

```cpp
class BlockTypeInfoSet {
    std::vector<BlockTypeInfo> m_blockTypeInfoSet; // 256개 블록 정보

    BlockTypeInfoSet() {
        m_blockTypeInfoSet[BLOCK_GRASS].Init(
            1,      // texTopIndex (잔디 상단)
            2,      // texSideIndex (흙+잔디 측면)
            3,      // texBottomIndex (흙 하단)
            false,  // isTransparency
            true,   // isOpaque
            false   // isSemiAlpha
        );
        // ... 256개 블록 초기화
    }
};
```

- 생성자에서 모든 블록 타입의 속성을 한 번에 초기화
- `GetInfo(type)` 메서드로 블록 정보 조회
- 단일 정보 소스(Single Source of Truth) 패턴

### 3.2 3단계 블록 선택 로직

```
GetBlockType(x, y, z, 기후 파라미터, distribution, elevation)
    ├─ 1단계: 특수 케이스 처리
    │   ├─ y == MIN_HEIGHT_LEVEL → BEDROCK (기반암)
    │   ├─ y <= WATER_HEIGHT_LEVEL → WATER (물)
    │   └─ y == WATER_HEIGHT_LEVEL && temperature < 0.25 → ICE (얼음)
    │
    ├─ 2단계: BiomeLayer 계산 (표면층 두께)
    │   biomeLayer = 2 + 6 × (1 - erosion) × sqrt((-peaksValley + 1) / 2)
    │   → 침식도가 낮고 봉우리일수록 두꺼운 표면층 (2~8블록)
    │
    └─ 3단계: 블록 선택
        ├─ y <= elevation - biomeLayer → GetBlockTypeForInner() (지하)
        └─ elevation - biomeLayer < y < elevation → GetBlockTypeForBiome() (표면)
```

### 3.3 밀도 기반 지하 블록 분포 (GetBlockTypeForInner)

```cpp
density = Terrain::GetDensity(x, y, z); // 3D 노이즈

if (density <= 0.3)        → DIRT (흙)
else if (density <= 0.75)  → STONE (돌)
else if (density <= 0.88)  → ANDESITE (안산암)
else {  // 광석 분포 (density > 0.88)
    if (distribution <= 0.4)       → COAL_ORE (석탄) - 40%
    else if (distribution <= 0.6)  → COPPER_ORE (구리) - 20%
    else if (distribution <= 0.7)  → IRON_ORE (철) - 10%
    else if (distribution <= 0.8)  → REDSTONE_ORE (레드스톤) - 10%
    else if (distribution <= 0.9)  → GOLD_ORE (금) - 10%
    else                           → DIAMOND_ORE (다이아몬드) - 10%
}
```

- `density > 0.88`일 때만 광석 생성 → 희소성 보장
- `distribution` 값으로 광석 종류 확률적 결정
- 석탄이 가장 흔하고 다이아몬드가 가장 희귀

### 3.4 바이옴별 표면 블록 구성 (GetBlockTypeForBiome)

```cpp
switch (biomeType) {
    case BIOME_OCEAN:
        if (d <= 0.21)      → SAND
        else if (d <= 0.66) → DIRT
        else if (d <= 0.83) → GRAVEL
        else                → CLAY

    case BIOME_DESERT:
        if (d <= 0.66) → SAND
        else           → SANDSTONE

    case BIOME_TUNDRA:
        if (y == baseHeight) { // 최상층
            if (d <= 0.2)      → ICE
            else if (d <= 0.6) → SNOW
            else               → SNOW_GRASS
        }
        else if (y == baseHeight - 1) {
            if (d <= 0.6) → SNOW_GRASS
            else          → DIRT
        }
        else → COARSE or DIRT

    case BIOME_SWAMP:
        if (y == baseHeight) {
            if (d <= 0.25)      → MUD (진흙)
            else if (d <= 0.4)  → MOSS (이끼)
            else if (d <= 0.9)  → GRASS
            else                → DIRT
        }

    case BIOME_PLAINS / FOREST / SAVANNA:
        if (y == baseHeight) {
            if (d <= 0.95) → GRASS
            else           → DIRT
        }
}
```

- `baseHeight = floor(elevation)` - 바이옴의 기본 높이
- `y == baseHeight`일 때 최상층 블록 (풀, 눈, 모래 등)
- `y < baseHeight`일 때 하부층 블록 (흙, 돌 등)
- `distribution(d)` 값으로 동일 바이옴 내 블록 다양성 확보

## 4. 구현 내용

### 4.1 블록 타입 분류 (주요 카테고리)

| 카테고리      | 블록 예시                      | 개수  | 특징                                   |
| ------------- | ------------------------------ | ----- | -------------------------------------- |
| **지형 블록** | GRASS, DIRT, STONE, SAND       | ~25개 | isOpaque=true, 가장 흔함               |
| **광석**      | COAL, IRON, GOLD, DIAMOND      | 6개   | isOpaque=true, density>0.88에서만 생성 |
| **나무**      | OAK_LOG, SPRUCE_LOG, BIRCH_LOG | 8개   | isOpaque=true, 바이옴별 나무 생성      |
| **나뭇잎**    | OAK_LEAF, SPRUCE_LEAF          | 7개   | isSemiAlpha=true, 반투명 렌더링        |
| **특수**      | WATER, AIR, ICE                | 3개   | isTransparency=true, 특수 처리         |

### 4.2 BlockTypeInfo 구조

```cpp
class BlockTypeInfo {
private:
    uint8_t m_texTopIndex;      // 상단 면 텍스처 인덱스
    uint8_t m_texSideIndex;     // 측면 텍스처 인덱스
    uint8_t m_texBottomIndex;   // 하단 면 텍스처 인덱스
    bool m_isTransparency;      // 완전 투명 (AIR, WATER)
    bool m_isOpaque;            // 불투명 (대부분 블록)
    bool m_isSemiAlpha;         // 반투명 (나뭇잎)

public:
    uint8_t GetTexIndex(uint8_t face) const {
        if (face == TOP)    return m_texTopIndex;
        if (face == BOTTOM) return m_texBottomIndex;
        return m_texSideIndex;
    }
};
```

- 면(face)별로 다른 텍스처 인덱스 반환
  - 예: GRASS는 top(1), side(2), bottom(3) - 상단은 풀, 측면은 흙+풀, 하단은 흙
  - 예: STONE은 모든 면이 동일 (6, 6, 6)
- 투명도 플래그는 렌더링 패스 분리에 사용
  - Opaque: 기본 불투명 패스
  - SemiAlpha: 반투명 패스 (알파 블렌딩)
  - Transparency: 스킵 또는 특수 처리 (물, 공기)

### 4.3 BiomeLayer 계산식

```cpp
biomeLayer = 2 + (int)(6.0f * (1.0f - erosion) * pow(((-peaksValley + 1.0f) * 0.5f), 0.5f))
```

| 조건        | erosion | peaksValley | biomeLayer | 의미          |
| ----------- | ------- | ----------- | ---------- | ------------- |
| 침식된 계곡 | 0.8     | 0.5         | 2블록      | 얇은 표면층   |
| 일반 평지   | 0.5     | 0.0         | 4블록      | 보통 표면층   |
| 봉우리      | 0.2     | -0.8        | 7블록      | 두꺼운 표면층 |

- 침식도가 낮을수록 (`1 - erosion` 크면) 표면층 두껍게
- 봉우리일수록 (`-peaksValley` 크면) 표면층 두껍게
- 2~8블록 범위로 제한

### 4.4 주요 함수

**`GetBlockType(x, y, z, continentalness, erosion, pv, t, h, dist, elev)`**

```cpp
// 1. 특수 케이스
if (y == MIN_HEIGHT_LEVEL) return BEDROCK;
if (y <= WATER_HEIGHT_LEVEL) blockType = WATER;
if (y == WATER_HEIGHT_LEVEL && temperature < 0.25f) blockType = ICE;

// 2. 동굴 체크 & BiomeLayer 계산
if (y < elevation && !IsCave(x, y, z)) {
    biomeLayer = 2 + (int)(6.0f * (1-erosion) * pow(((-pv+1)/2), 0.5f));

    // 3. 지하 vs 표면
    if (y <= elevation - biomeLayer) {
        blockType = GetBlockTypeForInner(x, y, z, dist);
    } else {
        BIOME_TYPE biome = Biome::GetBiomeType(c, e, t, h, x, z);
        blockType = GetBlockTypeForBiome(biome, y, elev, dist);
    }
}
```

**`GetBlockTextureIndex(type, face)`**

```cpp
return m_blockTypeInfoSet.GetInfo(type).GetTexIndex(face);
```

- 렌더링 시 텍스처 아틀라스에서 해당 블록의 면 텍스처 조회

**`IsTransparency(type)` / `IsOpaque(type)` / `IsSemiAlpha(type)`**

```cpp
return m_blockTypeInfoSet.GetInfo(type).IsTransparency();
```

- 렌더링 패스 분리를 위한 쿼리 함수

## 5. 문제점 & 해결

### 5.1 광석 분포가 너무 흔함

**문제**: 초기 구현에서 모든 지하 블록 중 일정 비율을 광석으로 배치 → 광석이 너무 쉽게 발견됨

**해결**:

- `density > 0.88`일 때만 광석 생성 → 전체 블록의 약 12%만 광석 후보
- 그 중에서도 `distribution` 값으로 확률적 배치
- 결과: 다이아몬드는 전체 블록의 약 1.2% (희소성 확보)

### 5.2 바이옴 경계에서 블록 급격한 변화

**문제**: 바이옴 경계에서 GRASS → SAND로 1블록 단위 전환 시 부자연스러움

**해결**:

- BiomeLayer를 2~8블록으로 설정하여 점진적 전환 구간 생성
- 침식도와 봉우리/계곡 값으로 동적 계산 → 지형에 따라 자연스러운 두께
- 바이옴 경계 근처는 높이 블렌딩(Biome 시스템)과 함께 작동하여 부드러운 전환

### 5.3 표면 블록이 단조로움

**문제**: 특정 바이옴 내 모든 표면이 동일 블록 (예: Plains 전부 GRASS)

**해결**:

- `distribution(d)` 값 활용하여 확률적 블록 선택
- 예: Taiga에서 `d <= 0.7` → GRASS, `0.7 < d <= 0.85` → PODZOL, `d > 0.85` → COARSE
- 예: Swamp에서 `d <= 0.25` → MUD, `0.25 < d <= 0.4` → MOSS, `d > 0.4` → GRASS
- 결과: 동일 바이옴 내에서도 시각적 다양성 확보

### 5.4 투명도 렌더링 성능 문제

**문제**: 모든 블록을 동일한 렌더링 패스로 처리 시 오버드로우 증가

**해결**:

- 3가지 투명도 플래그로 렌더링 패스 분리
  - **isOpaque=true**: 기본 불투명 패스 (대부분 블록)
  - **isSemiAlpha=true**: 반투명 패스 (나뭇잎, 알파 블렌딩 필요)
  - **isTransparency=true**: 스킵 또는 특수 처리 (AIR, WATER)
- ChunkManager에서 블록 타입별로 다른 버퍼에 메시 저장

### 5.5 Y축 기반 광석 분포 부재

**문제**: 현실에서는 다이아몬드가 깊은 곳에만 생성되지만, 현재는 모든 높이에서 동일 확률

**해결 (부분적)**:

- 현재는 `density > 0.88` 조건만으로 희소성 제어
- **향후 개선 방향**: Y축 높이에 따른 광석 분포 가중치 추가
  - 예: `y < 16`에서만 다이아몬드 생성
  - 예: `y > 64`에서 석탄 더 흔하게

## 6. 결과

### 6.1 블록 타입 다양성

- **총 256개 블록 타입 지원** (현재 ~40개 정의됨, 확장 가능)
- **3종류 투명도 모드**로 렌더링 최적화
- **바이옴별 표면 구성**:
  - Ocean: 모래, 흙, 자갈, 점토
  - Desert: 모래, 사암
  - Tundra: 얼음, 눈, 눈 덮인 잔디
  - Swamp: 진흙, 이끼, 잔디
  - Plains/Forest: 잔디, 흙

### 6.2 광석 분포

| 광석       | 생성 확률 | 희소성    |
| ---------- | --------- | --------- |
| 석탄       | ~4.8%     | 흔함      |
| 구리       | ~2.4%     | 보통      |
| 철         | ~1.2%     | 보통      |
| 레드스톤   | ~1.2%     | 희귀      |
| 금         | ~1.2%     | 희귀      |
| 다이아몬드 | ~1.2%     | 매우 희귀 |

- `density > 0.88` 조건으로 전체 블록의 약 12%만 광석 후보
- 그 중 `distribution` 값으로 광석 종류 결정

### 6.3 BiomeLayer 효과

- **침식된 지형**: biomeLayer = 2~3블록 → 얇은 표면층, 빠르게 암반 노출
- **평탄한 지형**: biomeLayer = 4~5블록 → 보통 두께, 안정적인 표면
- **봉우리 지형**: biomeLayer = 6~8블록 → 두꺼운 표면층, 암반 깊이 묻힘

### 6.4 성능

- `GetBlockType()`: 블록마다 1회 호출 → 청크 생성 시에만 실행
- `BlockTypeInfoSet`: 정적 싱글톤으로 메모리 효율적
- 텍스처 인덱스 조회: O(1) 배열 접근
- 런타임 성능 영향 없음 (월드 생성 단계에서만 실행)

## 7. 회고

### 7.1 아쉬운 점

- **블록 타입 하드코딩**: 256개 블록이 `BlockTypeInfoSet` 생성자에 하드코딩되어 확장성 낮음
  - 새 블록 추가 시 `Enums.h`의 `BLOCK_TYPE` enum과 `BlockTypeInfoSet` 생성자 모두 수정 필요
- **BiomeLayer 공식의 복잡성**: `2 + 6 × (1-erosion) × sqrt((1-pv)/2)` 공식이 직관적이지 않아 튜닝 어려움
  - 매직 넘버(2, 6) 의미 불명확
- **광석 분포의 단순성**: Y축 고려 없이 density 임계값만으로 결정
  - 현실적인 광석 분포(다이아몬드는 깊은 곳에만) 구현 부재
- **블록 변이(Variant) 부재**: 동일 블록의 미세한 변종 없음
  - 예: Stone의 균열 있는 버전, Grass의 긴/짧은 버전
- **텍스처 인덱스 하드코딩**: 텍스처 아틀라스 인덱스가 코드에 하드코딩되어 있어 텍스처 재배치 시 전체 수정 필요

### 7.2 다음에 개선하고 싶은 방향

- **설정 파일 기반 블록 정의**: JSON/TOML로 블록 속성 외부화
  - 텍스처 인덱스, 투명도, 생성 규칙을 데이터 파일로 관리
- **Y축 기반 광석 분포**: 높이에 따른 광석 출현 확률 가중치
  - 다이아몬드: y < 16에서만
  - 석탄: 모든 높이 균일
  - 금: y < 32에서 더 흔함
- **BiomeLayer 공식 단순화**: 명확한 파라미터로 리팩토링
  - `minLayer`, `maxLayer`, `erosionWeight`, `peaksValleyWeight`로 분리
- **블록 변이 시스템**: 동일 블록의 미세한 시각적 변종 추가
  - Perlin 노이즈로 텍스처 인덱스에 오프셋 적용
- **동적 블록 로딩**: 런타임에 블록 타입 추가/제거 가능하도록 리팩토링
- **텍스처 아틀라스 메타데이터**: 텍스처 인덱스를 별도 메타데이터 파일에서 로드
