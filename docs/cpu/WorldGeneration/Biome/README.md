# Biome System

<table>
  <tr>
    <td><img src="https://github.com/user-attachments/assets/c80ceee9-6122-4d66-945e-3d1babc3d994"/></td>
    <td><img src="https://github.com/user-attachments/assets/0f83b205-7cd0-4134-8701-83caf40a989c"/></td>
    <td><img src="https://github.com/user-attachments/assets/a0ae2757-4436-4fd1-a161-b323f5c2c1af"/></td>
  </tr>
  <tr>
    <td><img src="https://github.com/user-attachments/assets/6461034d-f03d-4870-a2c8-8c6ed04de9d6"/></td>
    <td><img src="https://github.com/user-attachments/assets/dc1c5828-ca8f-41dc-bfc8-9b92df50e2f7"/></td>
    <td><img src="https://github.com/user-attachments/assets/277a4a6a-979d-48fb-88ae-59ec29684406"/></td>
  </tr>
  <tr>
    <td><img src="https://github.com/user-attachments/assets/369e4318-0eb9-4202-91a8-a61c52bf1a29"/></td>
    <td><img src="https://github.com/user-attachments/assets/685b6546-0bb6-4d8c-bc41-1d708ce8c1d0"/></td>
    <td><img src="https://github.com/user-attachments/assets/1218d0fc-58d3-46c6-8844-33f26819508b"/></td>
  </tr>
</table>


<table>
  <tr>
    <td><img src="https://github.com/user-attachments/assets/29dbc56d-b346-4384-98ef-522b448568b5" width="300"/></td>
    <td><img src="https://github.com/user-attachments/assets/1d46375d-23b1-4a75-8974-8f4c7c852d65" width="300"/></td>
    <td><img src="https://github.com/user-attachments/assets/15ae9b89-4e43-4570-a2f1-f51a77fdd51c" width="300"/></td>
  </tr>
  <tr>
    <td><img src="https://github.com/user-attachments/assets/02c3a34e-85b3-4e43-bb09-1d8caa18e1f3" width="300"/></td>
    <td><img src="https://github.com/user-attachments/assets/969852d7-2f94-45c7-b247-8adb3226cb28" width="300"/></td>
    <td><img src="https://github.com/user-attachments/assets/ad8ddcb6-1aff-46ef-bf49-3185492e59e7" width="300"/></td>
  </tr>
  <tr>
    <td><img src="https://github.com/user-attachments/assets/c07a63da-165d-4143-b0f8-46f57096a4fa" width="300"/></td>
    <td><img src="https://github.com/user-attachments/assets/fe39a9f6-023d-4d87-8bdd-86f05a4eb13d" width="300"/></td>
    <td><img src="https://github.com/user-attachments/assets/aad7bbbb-f044-425c-a9d0-f28cd9552dfd" width="300"/></td>
  </tr>
</table>

<br />

`4096 × 4096` 블록 스케일에서의 mix off / on:

<img width="2199" height="1035" alt="Image" src="https://github.com/user-attachments/assets/ee5a3eca-2617-4182-9db6-3c5982a7bea3" />

## 1. 개요

Voxen 은 12개의 biome (`OCEAN`, `TUNDRA`, `TAIGA`, `PLAINS`, `SWAMP`, `FOREST`, `SHRUBLAND`, `DESERT`, `RAINFOREST`, `SEASONFOREST`, `SAVANNA`, `SNOWY_TAIGA`) 을 정의하고,
각 xz 좌표에서 **가중 최근접(weighted-nearest)** 방식으로 하나의 biome을 결정한다.
결정된 biome 은 이후 지형의 최종 높이 결정, 지형 표면 블록(`biomeLayer`), 트리 종류, 인스턴스(풀/꽃) 종류를 모두 관여한다.

## 2. Biome 구조 — Biome → BiomeTypeInfoSet → BiomeTypeInfo

Voxen은 biome 을 클래스 상속(OOP) 으로 구성하지 않는다. 대신:

- `Biome` — **정적 함수만 노출하는 접근 계층**
- `BiomeTypeInfoSet` — 12개 `BiomeTypeInfo` 를 담는 컨테이너, 생성자에서 값 세팅
- `BiomeTypeInfo` — 한 biome 의 속성(baseColor / instances / trees / maxCount / weightParams) 을 담는 값 객체

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

`Biome::Get..()` 함수를 호출하면 멤버인 `m_biomeTypeInfoSet`에 접근하여 적절한 Type에 맞는 Info 객체를 가져오고 그 객체의 함수를 호출한다.

```cpp
// voxen/srcs/Biome.cpp
BiomeTypeInfoSet Biome::m_biomeTypeInfoSet;

RGBA_UINT Biome::GetBaseColor(BIOME_TYPE type)
{
    return m_biomeTypeInfoSet.GetInfo(type).GetBaseColor();
}
```

### 2.1 왜 OOP 를 쓰지 않았는가

Biome 마다 별도 클래스로 상속(`class OceanBiome : Biome` 등) 을 두는 방식
대신 **동일 형태의 데이터 객체 12개를 컨테이너에 넣는 방식** 을 택했다.

- Biome 의 차이는 **로직이 아니라 파라미터 값 차이**이다.
  (baseHeight, elevationScale, weight anchor, instance/tree 리스트)
- 상속을 도입하면 각 biome 마다 파라미터를 분산 저장하게 되어, 값이 퍼져있어 한 눈에 파악하기 힘들다.

`BiomeTypeInfoSet` 의 생성자는 12개 항목을 한 곳에 순차적으로 채워 넣어 데이터가 한 파일에서 존재하도록 했다.

- 새 biome 을 추가하거나 파라미터를 튜닝할 때 수정할 지점이 명확하다는 것이 이 구조의 실질적인 이점이다.

### 2.2 BiomeInfo 내용

```cpp
// BiomeInfo
class BiomeInfo {
    ...
private:
	RGBA_UINT m_baseColor;                   // biome 구별 색 -> 월드맵 렌더링을 위함
	uint32_t m_maxTreeCountPerChunk;         // 청크 당 트리 최대 개수
	uint32_t m_maxInstanceCountPerChunk;     // 청크 당 인스턴스 최대 개수
	std::vector<INSTANCE_TYPE> m_instances;  // 바이옴에 존재하는 instance type list
	std::vector<TREE_TYPE> m_trees;          // 바이옴에 존재하는 tree type list
	BiomeWeightParams m_weightParams;        // 바이옴 결정 가중치 기준 값
    float m_elevationScale;                  // 바이옴 높이 영향 스케일
    float m_baseHeight;                      // 바이옴 기본 높이
};
```

### 2.3 BiomeWeightParams

바이옴을 결정짓게 해주는 가중치 파라미터

- 바이옴 별로 각 기준치를 설정
- 모든 바이옴을 순회하여 기준치 값에 맞는지 판단 후 가장 가까운 바이옴을 선택하게 한다.

```
struct BiomeWeightParams {
	float continentalness; // 대륙: 해양 바이옴 제외 고정 0.7
	float erosion;         // 침식: 고정 0.5 -> 사용처 못찾음
	float temperature;     // 온도
	float humidity;        // 습도
};
```

### 2.4 Biome별 데이터

12개 biome 각각의 `BiomeTypeInfo` 값 전체 (`BiomeTypeInfoSet` 생성자에서 등록되는 값 그대로).

| BIOME          |  c  |  e  |   t   |   h   | baseHeight | elevScale | maxTree | maxInst |
| -------------- | :-: | :-: | :---: | :---: | :--------: | :-------: | :-----: | :-----: |
| `OCEAN`        | 0.0 | 0.5 |  0.5  |  0.5  |     32     |   0.025   |    0    |   64    |
| `TUNDRA`       | 0.7 | 0.5 | 0.125 |  0.2  |    108     |   0.025   |    3    |    0    |
| `TAIGA`        | 0.7 | 0.5 | 0.34  | 0.66  |     96     |   0.25    |    8    |   64    |
| `PLAINS`       | 0.7 | 0.5 | 0.435 | 0.16  |     64     |   0.025   |    4    |   96    |
| `SWAMP`        | 0.7 | 0.5 | 0.53  | 0.88  |     56     |   0.025   |   12    |   128   |
| `FOREST`       | 0.7 | 0.5 | 0.53  | 0.66  |     64     |    1.5    |   32    |   160   |
| `SHRUBLAND`    | 0.7 | 0.5 | 0.53  | 0.44  |     64     |   0.01    |    3    |   96    |
| `DESERT`       | 0.7 | 0.5 | 0.81  | 0.125 |     80     |   0.01    |    1    |    4    |
| `RAINFOREST`   | 0.7 | 0.5 | 0.84  | 0.88  |     64     |    1.5    |   24    |   160   |
| `SEASONFOREST` | 0.7 | 0.5 | 0.84  | 0.66  |     64     |    1.5    |   32    |   160   |
| `SAVANNA`      | 0.7 | 0.5 | 0.84  | 0.44  |     70     |   0.01    |    3    |   64    |
| `SNOWY_TAIGA`  | 0.7 | 0.5 | 0.28  | 0.66  |     96     |   0.75    |    2    |   32    |

Trees / Instances 후보 목록:

| BIOME          | Trees         | Instances                                                           |
| -------------- | ------------- | ------------------------------------------------------------------- |
| `OCEAN`        | _(none)_      | seagrass, kelp                                                      |
| `TUNDRA`       | spruce        | _(none)_                                                            |
| `TAIGA`        | spruce        | grass, fern, sweet_berry_bush                                       |
| `PLAINS`       | oak           | grass, oxeye_daisy, corn_flower, tulip ×4 (pink/red/white/orange)   |
| `SWAMP`        | oak, mangrove | grass, blue_orchid, mushroom_brown, mushroom_red, dead_bush         |
| `FOREST`       | oak, birch    | grass, rose_blue, rose_red, rose_plants, lily_of_the_valley, allium |
| `SHRUBLAND`    | oak, cherry   | grass, dandelion, corn_flower, allium, oxeye_daisy, tulip ×4        |
| `DESERT`       | cactus        | dead_bush                                                           |
| `RAINFOREST`   | oak, jungle   | grass, fern                                                         |
| `SEASONFOREST` | oak, birch    | grass, allium, lily_of_the_valley, rose_plants, tulip ×4            |
| `SAVANNA`      | oak, acacia   | grass                                                               |
| `SNOWY_TAIGA`  | spruce        | grass, fern, sweet_berry_bush                                       |

## 3. Biome 결정 방식

### 3.1 6개 base 노이즈 중 4개(`c` / `e` / `t` / `h`)만 사용

biome 결정 입력은 6개 base 노이즈 중 `c` 대륙 / `e` 침식 / `t` 온도 / `h` 습도 4개다.

cf. 실질적인 구분은 t, h로 한다.

- 대륙(`c`)값은 해양 바이옴만 구분, 침식(`e`)값은 0.5로 고정이라 실질적인 도움이 되지 않는 상황이다.
- 차후를 위해서 일단 두었다.

```cpp
// Chunk::InitBiomeMapAndCount : Chunk 내부에서 Biome을 초기화하는 모습
BIOME_TYPE biomeType = Biome::GetBiomeType(
    memory->continentalinessNoises[x][z],
    memory->erosionNoises       [x][z],
    memory->temperatureNoises   [x][z],
    memory->humidityNoises      [x][z],
    worldX, worldZ);
memory->biomeMap2D[x][z] = biomeType;
```

### 3.2 가중치 기반 최근접 바이옴 선택 (weighted-nearest)

현재 좌표의 노이즈 값과 각 축 사이의 가중 거리를 구해서 가장 가까운 biome을 고른다.

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
  - `e` 도 활용하여 구분에 사용하려했으나, 현재는 그렇지 못한 관계로 가중치를 작게 사용
  - `t, h` 는 중간 무게로, 육지 안에서 다양한 biome 을 나누는 실질적 축이다.

12개 biome 앵커를 `(t, h)` 축에 나타내면 (c 는 대부분 0.7 고정, e 는 전부 0.5):

<img width="1083" height="1171" alt="Image" src="https://github.com/user-attachments/assets/83dce7bf-a255-49f3-b961-d577e9f678ac" />

축이 극단이면 특정 biome 이 명확히 이기고, 중앙 근처(`t ≈ 0.5, h ≈ 0.5`)에서는
여러 biome 이 비슷한 거리에 있어 아주 미묘한 노이즈 변화로도 다른 biome 이 선택된다.

### 3.3 Mix Biome — 경계 salt-and-pepper 처리

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

경계 확대(512 × 512 블록, 1 sample/block) 비교:

<img width="2227" height="1104" alt="Image" src="https://github.com/user-attachments/assets/da76491e-bca1-471f-86b6-0ecc62ac902b" />

- 왼쪽 (Mix OFF) —오른쪽 (Mix ON)
- OCEAN ↔ 육지 경계는 c 축 차이(0.0 vs 0.7) 가 압도적이라 threshold에 잘 걸리지 않아 매끈함
- **경계 성격에 따라 자동으로 mix 여부가 갈리는** 것이 이 방식의 장점

## 4. Biome 이 이후 파이프라인에 미치는 영향

`Chunk::InitBiomeMapAndCount` 로 `biomeMap2D` / `biomeCount` 가 채워지면, 이후 세 단계가 이 값을 사용한다.

### 4.1 Block — 지형 표면의 biomeLayer

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

- `biomeLayer`: 표면에서 몇블록까지 Biome 블록 층으로 볼지 결정
  - `e`, `pv`에 반비례
  - 침식이 높을 수록, 융기가 많을 수록 `biomeLayer`가 줄어든다.
- biome 마다 다른 표면 블록을 결정한다. 예시:
  - `PLAINS` : 위 3블록은 잔디, 그 아래는 흙
  - `DESERT` : 위 몇 블록은 모래, 그 아래는 사암
  - `OCEAN` : `d` 노이즈 값에 따라 모래 / 흙 / 자갈 / 점토 중 하나
  - `TUNDRA` / `SNOWY_TAIGA` : 눈 층
- 표면 층이 아닌 곳은 돌, 석탄, 광석 등으로 채운다.

### 4.2 Tree — biome 별 Tree 개수 및 종류

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

두 결정이 있다:

1. **개수** — `CanPlaceTreeAt` 안에서

- `Biome::GetMaxTreeCountPerChunk(biomeType) * (biomeCount[biomeType] / CHUNK_SIZE^2)`로 계산한다.
- `Biome::GetMaxTreeCountPerChunk(biomeType)`: FOREST 는 청크당 32그루까지, DESERT 는 1그루만 허용되는 식이다.
- `(biomeCount[biomeType] / CHUNK_SIZE^2)`: 청크 내부에 여러 개의 Biome이 존재할 수 있으므로, 바이옴의 비율을 정의한다.
- 이 둘을 곱해서 청크 내부에 존재할 수 있는 최대의 개수를 정의하여 사용한다.

2. **종류** — `Tree::GetTreeTypeForBiome(biomeType, d, ...)`

- 트리를 배치할 수 있다면 Biome에 결정된 트리 중 하나를 `d` 값 기준으로 결정한다.
- `SAVANNA` 라면 oak, acacia 중에 `d` 값을 기준으로 결정하게 된다.

### 4.3 Instance — biome 별 풀/꽃 등 개수 및 종류

트리와 완전히 같은 패턴이다.

### 4.4 사용처 요약

| 단계                  | biomeMap2D | biomeCount | maxTreeCount | maxInstanceCount | Trees list | Instances list |
| :-------------------- | :--------: | :--------: | :----------: | :--------------: | :--------: | :------------: |
| InitBasicBlockType    |     O      |            |              |                  |            |                |
| InitTreePlace         |     O      |     O      |      O       |                  |     O      |                |
| InitInstancePlace     |     O      |     O      |              |        O         |            |       O        |
| GetBiomeTerrainHeight |  \*(간접)  |            |              |                  |            |                |

`GetBiomeTerrainHeight` 는 4개 노이즈로부터 **가중 평균** 을 뽑으므로 `biomeMap2D` 를 직접 참조하지 않지만, 결국 같은 4개 노이즈에서 유도된다는 점에서 biome 결정과 같은 축을 쓴다.

## 5. 회고

Biome 구조 자체는 OOP 없이 잘 짠 것 같다.

- **파라미터 테이블 + 정적 접근자 조합**으로 로직은 같은데 값만 다른 경우 별다른 추가 코드 작성 없이 한 곳에 몰아서 작성할 수 있었다.
- Biome 추가 시 간단히 매개변수 위주로만 작성하면 되서 편했다.
- 하지만, 로직 자체가 달라진다면(특정 바이옴의 구성 방식이 혼자 다른 경우) OOP를 사용해야할 것이다.

Biome Blending 방식으로 보는 Blending 결과는 만족했다.

- Biome을 섞는 방식에 대해서 많은 고찰이 있었지만 해결해서 좋았다.
- 블렌딩 두께는 `threshold` 로, 밀도는 `hash % N` 의 N 값으로 각각 독립적으로 조절할 수 있었다.
- 또한, 이렇게 Biome Blending 방식을 가중치로 연산하는 상황에서 Biome에 대한 높이조절도 할 수 있는 아주 좋은 이점도 발견할 수 있었다.

미흡 및 아쉬운 점

- 생각보다 두드러진 Biome은 구성하지 못했다. 다 거기서 거기처럼 보이긴 한다.
- 예전 마인크래프트에서 정해진 Biome을 그대로 사용하기에 벅찼다.
- 해양을 제외하고 c, e 값으로는 현재 Biome을 구분하는 가중치 값이 되지 못하고 있다.
