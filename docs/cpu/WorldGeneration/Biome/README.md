# Biome System

Voxen 은 12개의 biome (`OCEAN`, `TUNDRA`, `TAIGA`, `PLAINS`, `SWAMP`, `FOREST`,
`SHRUBLAND`, `DESERT`, `RAINFOREST`, `SEASONFOREST`, `SAVANNA`, `SNOWY_TAIGA`) 을
정의하고, 각 xz 좌표에서 **가중 최근접(weighted-nearest)** 방식으로 하나의 biome
을 결정한다. 결정된 biome 은 이후 지형 표면 블록(`biomeLayer`), 트리 종류,
인스턴스(풀/꽃) 종류를 모두 좌우한다.

## 1. 개요

### 1.1 자료구조 — Biome → BiomeTypeInfoSet → BiomeTypeInfo

Voxen 은 biome 을 클래스 상속(OOP) 으로 구성하지 않는다. 그 대신

- `Biome` — **정적 함수만 노출하는 접근 계층 (facade)**
- `BiomeTypeInfoSet` — 12개 `BiomeTypeInfo` 를 담는 컨테이너, 생성자에서 값 세팅
- `BiomeTypeInfo` — 한 biome 의 속성(baseColor / instances / trees /
  maxCount / weightParams) 을 담는 값 객체

```cpp
// voxen/headers/Biome.h  (요약)
class Biome {
public:
    static RGBA_UINT                       GetBaseColor              (BIOME_TYPE);
    static uint32_t                        GetMaxInstanceCountPerChunk(BIOME_TYPE);
    static uint32_t                        GetMaxTreeCountPerChunk   (BIOME_TYPE);
    static const std::vector<INSTANCE_TYPE>& GetInstances            (BIOME_TYPE);
    static const std::vector<TREE_TYPE>&   GetTrees                  (BIOME_TYPE);
    static const BiomeWeightParams&        GetWeightParams           (BIOME_TYPE);
    static BIOME_TYPE                      GetBiomeType(float c, float e, float t, float h,
                                                       int x, int z);
    static float                           GetBiomeTerrainHeight(float c, float e, float pv,
                                                                 float t, float h);
private:
    static BiomeTypeInfoSet m_biomeTypeInfoSet;
};
```

`Biome::GetXxx()` 는 모두 아래처럼 **`m_biomeTypeInfoSet.GetInfo(type).GetXxx()`
로의 위임**이다. 즉 `Biome` 자체는 상태가 없고 전역 정적 테이블 하나(`m_biomeTypeInfoSet`)에
대한 얇은 접근자다.

```cpp
// voxen/srcs/Biome.cpp
BiomeTypeInfoSet Biome::m_biomeTypeInfoSet;

RGBA_UINT Biome::GetBaseColor(BIOME_TYPE type)
{
    return m_biomeTypeInfoSet.GetInfo(type).GetBaseColor();
}
...
```

### 1.2 왜 OOP 를 쓰지 않았는가

Biome 마다 별도 클래스로 상속(`class OceanBiome : Biome` 등) 을 두는 방식
대신 **동일 형태의 데이터 객체 12개를 컨테이너에 넣는 방식** 을 택했다.

- Biome 의 차이는 **로직이 아니라 파라미터의 차이**이다.
  (baseHeight, elevationScale, weight anchor, instance/tree 리스트)
- 로직 자체 (biome 결정, 지형 높이 합성) 는 모든 biome 이 공유하며,
  파라미터의 가중 평균으로 자연스럽게 섞을 수 있어야 한다.
- 상속을 도입하면 각 biome 마다 virtual override 를 만들거나 파라미터를
  분산 저장하게 되어, 가중 평균을 하려면 다시 값을 모아야 해서 오히려 복잡해진다.

`BiomeTypeInfoSet` 의 생성자는 12개 항목을 한 곳에 순차적으로 채워 넣어
데이터가 한 파일에서 스캔되도록 만들었다. 새 biome 을 추가하거나 파라미터를
튜닝할 때 편집 지점이 명확하다는 것이 이 구조의 실질적인 이점이다.

## 2. Biome 결정 방식

### 2.1 6개 base 노이즈 중 4개만 사용

biome 결정 입력은 6개 base 노이즈 중 `c` / `e` / `t` / `h` 4개다
(`pv` 는 지형 요철 전용, `d` 는 블록/인스턴스 variant 선택 전용).

```cpp
// Chunk::InitBiomeMapAndCount 발췌
BIOME_TYPE biomeType = Biome::GetBiomeType(
    memory->continentalinessNoises[x][z],
    memory->erosionNoises       [x][z],
    memory->temperatureNoises   [x][z],
    memory->humidityNoises      [x][z],
    worldX, worldZ);
memory->biomeMap2D[x][z] = biomeType;
```

biome 은 청크당 34 × 34 (padded) 로 `biomeMap2D` 에 캐싱되고, 이 중 32 × 32
내부는 `biomeCount[biomeType]++` 로 카운트도 함께 세어 둔다.
카운트는 이후 `InitTreePlace`/`InitInstancePlace` 에서 "이 청크에 이 biome
비율만큼 트리/인스턴스를 배치" 하는 quota 계산에 쓰인다.

### 2.2 가중 최근접 (weighted-nearest)

각 biome 은 4차원 특성 공간의 **앵커** `(c, e, t, h)` 를 갖는다.
현재 좌표의 노이즈 값과 각 앵커 사이의 가중 거리를 구해서 가장 가까운 biome 을 고른다.

```cpp
// Biome::GetBiomeType 발췌
float coEffi[4] = { 10.0f, 1.0f, 3.0f, 3.0f };   // c, e, t, h 축의 중요도
for (int i = 0; i < BIOME_COUNT; ++i) {
    const auto& p = GetWeightParams((BIOME_TYPE)i);
    float cDiff = coEffi[0] * pow(fabs(c - p.continentalness), 2);
    float eDiff = coEffi[1] * pow(fabs(e - p.erosion         ), 2);
    float tDiff = coEffi[2] * pow(fabs(t - p.temperature     ), 2);
    float hDiff = coEffi[3] * pow(fabs(h - p.humidity        ), 2);
    float diff  = sqrt(cDiff + eDiff + tDiff + hDiff);
    ...
}
```

- 계수 `10, 1, 3, 3` — `c` (대륙성) 를 가장 무겁게, `e` (침식) 를 가장 가볍게.
  이 값이 결과 지도의 성격을 크게 좌우한다:
  - `c` 가 무거우므로 OCEAN(c=0) 과 육지(c=0.7) 는 거의 항상 분리된다.
  - `e` 가 가벼우므로 침식 노이즈가 biome 을 바꾸지는 못하고, 오직
    같은 biome 안에서 지형만 완만하게 만든다.
  - `t, h` 는 중간 무게로, 육지 안에서 다양한 biome 을 나누는 실질적 축이다.

12개 biome 앵커를 `(t, h)` 축에 나타내면 (c 는 대부분 0.7 고정, e 는 전부 0.5):

<img width="1083" height="1171" alt="Image" src="https://github.com/user-attachments/assets/83dce7bf-a255-49f3-b961-d577e9f678ac" />

축이 극단이면 특정 biome 이 명확히 이기고, 중앙 근처(`t ≈ 0.5, h ≈ 0.5`)에서는
여러 biome 이 비슷한 거리에 있어 아주 미묘한 노이즈 변화로도 다른 biome 이 선택된다.

### 2.3 Mix Biome — 경계 salt-and-pepper 처리

가중 최근접만 사용하면 두 biome 이 만나는 경계는 **매끄러운 곡선**이 된다.
Voxen 은 이 경계를 조금 흐트러뜨려 "블렌딩" 느낌을 내기 위해
아주 좁은 tie 구간에서 무작위로 2nd 후보를 뽑는 salt-and-pepper 방식을 쓴다.

```cpp
// Biome::GetBiomeType 마무리
std::sort(diffs.begin(), diffs.end(), ...);   // 가까운 순 정렬

float diffOfTop = fabs(diffs[0].first - diffs[1].first);
float threshold = 0.005f;

if (diffOfTop < threshold) {                  // 1등과 2등이 사실상 동률일 때만
    uint32_t hashX = Utils::HashInt(x, 27551u);
    uint32_t hashZ = Utils::HashInt(z, 35251u);
    uint32_t hash  = hashX ^ hashZ;

    if (hash % 11 == 0) {
        return diffs[1].second;               // 11 블록 중 1 블록 확률로 2등 사용
    }
}
return diffs[0].second;
```

핵심 포인트:

- **`|diff0 - diff1| < 0.005`** 조건이 mix 가 적용되는 좁은 영역(진짜 경계) 을 잡아준다.
  경계에서 멀면 1등이 압도적이라 mix 는 아무 영향도 주지 않는다.
- **hash % 11 == 0** — 좌표 해시 기반이므로 결정성이 있다.
  같은 위치는 항상 같은 결과를 낸다 (청크 재로드해도 동일).
- 확률 1/11 정도가 시각적으로 자연스러운 salt-and-pepper 를 만든다.
  더 자주(1/2, 1/4) 뽑으면 오히려 얼룩덜룩해 보이고, 너무 드물게(1/50) 뽑으면
  블렌딩 느낌이 안 산다.

경계 확대(512 × 512 블록, 1 sample/block) 비교:

<img width="2227" height="1104" alt="Image" src="https://github.com/user-attachments/assets/da76491e-bca1-471f-86b6-0ecc62ac902b" />

- 왼쪽 (Mix OFF) — 딱딱한 경계 곡선
- 오른쪽 (Mix ON) — SHRUBLAND ↔ SAVANNA, PLAINS ↔ SEASONFOREST 처럼 앵커 거리가
  가까운 육지-육지 경계에서 stippling 발생
- OCEAN ↔ 육지 경계는 c 축 차이(0.0 vs 0.7) 가 압도적이라 tie 조건에 걸리지
  않아 여전히 매끈함 → **경계 성격에 따라 자동으로 mix 여부가 갈리는** 것이 이 방식의 장점

## 3. Biome 이 이후 파이프라인에 미치는 영향

`Chunk::InitBiomeMapAndCount` 로 `biomeMap2D` / `biomeCount` 가 채워지면, 이후 세 단계가 이 값을 사용한다.

### 3.1 Block — 지형 표면의 biomeLayer

`Block::GetBlockType` 은 y 축 순회 시 두 케이스로 나뉜다.

```cpp
// voxen/srcs/Block.cpp
if (y < elevation && !Terrain::IsCave(x, y, z)) {
    int biomeLayer =
        2 + (int)(6.0f * (1.0f - erosion) * powf(((-peaksValley + 1.0f) * 0.5f), 0.5f));

    if (y <= elevation - biomeLayer) {
        blockType = GetBlockTypeForInner(x, y, z, distribution);       // 내부: 돌, 석탄, 광석 등
    } else {
        blockType = GetBlockTypeForBiome(biomeType, y, elevation, distribution);  // 표면: biome 별 블록
    }
}
```

- **biomeLayer** = 표면에서 몇 블록 아래까지 "지표층" 으로 볼지.
  침식이 강할수록 (`(1-e)` 작음) 얇고, 험한 지형(`pv` 큼)에서 두꺼워진다. 최소 2 블록.
- 표면층에서는 `Block::GetBlockTypeForBiome(biomeType, ...)` 로 넘어가서
  biome 마다 다른 표면 블록을 결정한다. 예시:
  - `PLAINS` : 위 3블록은 잔디, 그 아래는 흙
  - `DESERT` : 위 몇 블록은 모래, 그 아래는 사암
  - `OCEAN` : `d` 노이즈 값에 따라 모래 / 흙 / 자갈 / 점토 중 하나
  - `TUNDRA` / `SNOWY_TAIGA` : 눈 층

즉 biome 은 **지표 두께(간접)** 와 **지표 재질(직접)** 을 동시에 좌우한다.

### 3.2 Tree — biome 별 트리 종류 & 배치 quota

```cpp
// voxen/srcs/Chunk.cpp  (InitTreePlace)
BIOME_TYPE biomeType = memory->biomeMap2D[x + 1][z + 1];
...
if (CanPlaceTreeAt(x, localY, z, placedBiomeTreeCount[biomeType], memory)) {
    TREE_TYPE treeType = Tree::GetTreeTypeForBiome(
        biomeType, memory->distributionNoises[x + 1][z + 1], x, localY, z);
    PlaceTree(x, localY, z, memory, treeType);
    placedBiomeTreeCount[biomeType]++;
}
```

두 층의 결정이 있다:

1. **quota** — `CanPlaceTreeAt` 안에서
   `Biome::GetMaxTreeCountPerChunk(biomeType) * biomeCount[biomeType] / CHUNK_SIZE^2`
   비율로 최대 개수를 계산한다.
   FOREST 는 청크당 32그루까지, DESERT 는 1그루만 허용되는 식이다.
2. **종류** — `Tree::GetTreeTypeForBiome(biomeType, d, ...)` 가 이 biome 의 트리
   후보 목록(`Biome::GetTrees(biomeType)`) 중 하나를 `d` (distribution) 노이즈로 고른다.
   예: SWAMP 는 `d < 0.3` 이면 mangrove, 아니면 oak.

트리 후보 목록은 `BiomeTypeInfoSet` 생성자에서 biome 마다 미리 push_back 되어 있다.

### 3.3 Instance — biome 별 풀/꽃 종류 & 배치 quota

트리와 완전히 같은 패턴이다.

```cpp
// voxen/srcs/Chunk.cpp  (GetBiomeInstanceType)
BIOME_TYPE biomeType = memory->biomeMap2D[x + 1][z + 1];
float distribution   = memory->distributionNoises[x + 1][z + 1];
...
INSTANCE_TYPE instanceType =
    Instance::GetInstanceTypeForBiome(biomeType, distribution, worldPosInt3);
```

- quota — `Biome::GetMaxInstanceCountPerChunk(biomeType)` × biome 비율
  (FOREST 160, RAINFOREST 160, DESERT 4 등)
- 종류 — `Biome::GetInstances(biomeType)` 목록에서 `d` 값에 따라 선택
  (PLAINS 는 grass / oxeye daisy / cornflower / 여러 tulip, DESERT 는 dead bush 만 등)

### 3.4 사용처 요약

| 단계                  | biomeMap2D | biomeCount | maxTreeCount | maxInstanceCount | Trees list | Instances list |
| :-------------------- | :--------: | :--------: | :----------: | :--------------: | :--------: | :------------: |
| InitBasicBlockType    |     O      |            |              |                  |            |                |
| InitTreePlace         |     O      |     O      |      O       |                  |     O      |                |
| InitInstancePlace     |     O      |     O      |              |        O         |            |       O        |
| GetBiomeTerrainHeight |  \*(간접)  |            |              |                  |            |                |

`GetBiomeTerrainHeight` 는 argmin 대신 4개 노이즈로부터 **가중 평균** 을 뽑으므로 `biomeMap2D` 를 직접 참조하지 않지만, 결국 같은 4개 노이즈에서 유도된다는 점에서 biome 결정과 같은 축을 쓴다.

## 4. 참고 — 전역 biome map

`4096 × 4096` 블록 스케일에서의 mix off / on 비교:

<img width="2199" height="1035" alt="Image" src="https://github.com/user-attachments/assets/ee5a3eca-2617-4182-9db6-3c5982a7bea3" />

전역 스케일에서는 mix 유무의 차이가 미묘하지만, 확대해서 육지-육지 경계를 보면
salt-and-pepper 패턴이 명확히 드러난다 ([boundary zoom](#23-mix-biome--경계-salt-and-pepper-처리) 참고).

## 5. 회고

- **파라미터 테이블 + 정적 접근자 조합**은 이런 종류(카테고리마다 값이 다르지만 로직은 같음)에
  가장 편한 구조였다. 새 biome 추가 시 편집 지점이 `BiomeTypeInfoSet` 생성자 한 군데다.
  반면 로직이 biome 별로 근본적으로 달라진다면 (예: 어떤 biome 은 지형 생성 방식 자체가 다르다면)
  이 구조는 부적합해진다. 그럴 땐 상속이 낫다.

- **가중 최근접 + tie salt-and-pepper** 는 구현이 짧고 튜닝 손잡이가 명확하다는 게 최대 장점이다.
  블렌딩 두께는 `threshold` 로, 얼룩 밀도는 `hash % N` 의 N 값으로 각각 독립적으로 조절할 수 있다.
  더 부드러운 경계 (per-block interpolation, splatmap 등) 를 원하면 CPU 비용이 크게 늘어난다.

- **아쉬운 점** : 12개 biome 중 10개가 c=0.7, e=0.5 로 완전히 같은 값을 갖는다.
  즉 실제로는 (t, h) 2D 공간에서만 biome 이 나뉜다.
  c/e 를 biome 축으로 활용 못 하는 셈이라, 예를 들어 "동일한 온/습도인데 대륙 안쪽 vs 해안"
  같은 구분을 넣기 어렵다. c 앵커를 여러 층 (예: coastal 0.3, inland 0.9) 으로 분리하면
  훨씬 다양한 배치가 가능할 것.

- **개선하고 싶은 방향** : Mix Biome 이 tie 조건일 때만 발동하므로, 앵커가 아주 가까운
  biome 쌍 (`FOREST`↔`SHRUBLAND` 등) 만 blending 이 잘 되고, 앵커가 먼 쌍
  (`DESERT`↔`SNOWY_TAIGA` 등) 은 딱딱한 경계만 남는다. Threshold 를 노이즈 기울기에
  비례하도록 만들면 앵커 거리와 무관하게 균일한 두께의 blending 대역을 얻을 수 있다.
