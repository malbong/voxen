# Terrain Generation

`Chunk::InitTerrainNoises()` 는 청크 하나의 34 × 34 (padded) 격자에 대해
7개의 노이즈(6개 base + 1개 elevation)를 미리 뽑아 `ChunkLoadMemory` 에 캐싱한다.
이후의 biome 결정, block type 결정, tree/instance 배치, 지형 높이 계산이
모두 이 캐시를 읽어 진행된다.

## 1. 개요

지형은 Minecraft 1.18+ 의 "노이즈 라우터" 방식을 참고하여 두 층으로 나뉜다.

- **base noise 층** : Perlin FBM 6개 (`c` / `e` / `pv` / `t` / `h` / `d`)
- **파생 값 층** : `spline` → `elevation` → `biomeBaseHeight + elevationScale * elevation`

`InitTerrainNoises()` 는 이 두 층을 채우는 단일 함수이며, 이후 파이프라인
(`InitBiomeMapAndCount → InitBasicBlockType → InitTreePlace → InitInstancePlace →
InitWorldVerticesData`) 은 모두 이 노이즈 캐시에 의존한다.

```cpp
// voxen/srcs/Chunk.cpp
void Chunk::InitTerrainNoises(ChunkLoadMemory* memory)
{
    for (int x = 0; x < CHUNK_SIZE_P; ++x) {
        for (int z = 0; z < CHUNK_SIZE_P; ++z) {
            int worldX = (int)m_offsetPosition.x + x - 1;
            int worldZ = (int)m_offsetPosition.z + z - 1;

            memory->continentalinessNoises[x][z] = Terrain::GetContinentalness(worldX, worldZ);
            memory->erosionNoises      [x][z] = Terrain::GetErosion      (worldX, worldZ);
            memory->peaksValleyNoises  [x][z] = Terrain::GetPeaksValley  (worldX, worldZ);
            memory->temperatureNoises  [x][z] = Terrain::GetTemperature  (worldX, worldZ);
            memory->humidityNoises     [x][z] = Terrain::GetHumidity     (worldX, worldZ);
            memory->distributionNoises [x][z] = Terrain::GetDistribution (worldX, worldZ);

            memory->elevationNoises[x][z] =
                Biome::GetBiomeTerrainHeight(
                    memory->continentalinessNoises[x][z],
                    memory->erosionNoises      [x][z],
                    memory->peaksValleyNoises  [x][z],
                    memory->temperatureNoises  [x][z],
                    memory->humidityNoises     [x][z]);
        }
    }
}
```

## 2. 도입 동기

단순 heightmap Perlin 한 장으로 지형을 뽑을 수 있지만 그렇게 하면

- 대륙과 해양의 스케일이 잘 분리되지 않고
- 침식된 평지와 뾰족한 산이 뒤섞이며
- 온도 / 습도 등 시각/생태 축이 지형 축에 종속되어 버린다.

노이즈를 **의미별로 분리**하고 각각 다른 scale / freq / octave 로 뽑으면
같은 좌표에서 얻은 독립적인 축 값을 조합하여 훨씬 풍부한 결과를 만들 수 있다.
Voxen 은 이 축을 6개로 잡고, 각각의 역할을 아래처럼 명확히 나눴다.

| 노이즈       | 역할                                                          | 최종 사용처                    |
| ------------ | ------------------------------------------------------------- | ------------------------------ |
| `c`  continentalness | 대륙성. 높으면 기본 높이가 오름                          | elevation, biome 가중치        |
| `e`  erosion         | 침식. `c` 와 `pv` 를 눌러 평탄화                         | elevation, biome 가중치        |
| `pv` peaks-valley    | 뾰족한 산 ↔ 협곡. 지형에 요철을 얹음                     | elevation                       |
| `t`  temperature     | 온도. 색 결정, 바이옴 및 블록 타입 결정                  | biome 가중치, block color/type  |
| `h`  humidity        | 습도. 색 결정, 바이옴 및 블록 타입 결정                  | biome 가중치, block color/type  |
| `d`  distribution    | 분산. 블록/인스턴스의 마지막 선택 시 결정 노이즈로 사용  | tree/instance/block variant     |
| `elevation`          | xz 기준 최종 지형 높이 (world Y)                          | 지형 표면, 트리 배치 기준 등    |

## 3. 핵심 아이디어

### 3.1 Perlin FBM 의 `scale` / `freq` / `octave`

`Utils::PerlinFbm(x, y, freq, octave)` 는 다음과 같이 정의된다.

```cpp
// voxen/headers/Utils.h
static float PerlinFbm(float x, float y, float freq, int octave)
{
    float amp = 1.0f;
    float noise = 0.0f;
    float aFactor = exp2(-0.85f);      // 약 0.5547

    for (int i = 0; i < octave; ++i) {
        noise += amp * GetPerlinNoiseFbm(x * freq, y * freq);
        freq *= 2.0f;
        amp  *= aFactor;
    }
    return noise;
}
```

호출부 (`Terrain::GetContinentalness` 예시):

```cpp
static float GetContinentalness(int x, int z)
{
    float scale = 1024.0f;
    float cNoise = Utils::PerlinFbm(x / scale, z / scale, 2.0f, 6);
    ...
}
```

여기서 각 파라미터의 의미:

- **`scale`** : world 좌표를 나누는 값. 결과 도메인이 `[0, scale] → [0, 1]` 로 압축된다.
  즉 scale 이 클수록 하나의 특징(예: 대륙 하나)이 넓게 퍼진다.
- **`freq`** : 위에서 얻은 `[0, 1]` 도메인을 곱해 `[0, freq]` 로 벌린다.
  Perlin 내부는 `floor(px)` 로 정수 격자를 잡으므로 도메인이 `[0, freq]` 이면
  격자는 `freq × freq` 로 쪼개진다. 즉 첫 옥타브에서 한 격자 셀의 world 길이는
  `scale / freq` 블록이 된다.
- **`octave`** : 매 반복마다 `freq *= 2` (격자 폭 절반) 와 `amp *= 2^-0.85` (기여도 감소)
  를 적용해 노이즈를 겹친다. 옥타브를 늘리면 큰 덩어리 위에 세부가 얹힌다.

예시 `scale = 1024, freq = 2, octave = 4` 일 때 각 옥타브가 world 를 어떻게 쪼개는지:

![scale / freq / octave 개념](../../../../tools/terrain_scale_freq.png)

이 관점에서 6개 노이즈의 특성을 정리하면:

| 노이즈 | scale | freq | oct | 첫 옥타브 셀 폭 | 특징                                       |
| ------ | ----: | ---: | --: | --------------: | ------------------------------------------ |
| `c`    | 1024  | 2    | 6   | 512 blocks      | 대륙/해양 규모의 큰 덩어리                 |
| `e`    | 2048  | 2    | 4   | 1024 blocks     | 가장 큰 스케일 — 대륙 전체를 덮는 완만한 변화 |
| `pv`   | 512   | 1.5  | 6   | 341 blocks      | 산맥/협곡 규모 — 6옥타브로 세부까지 침투   |
| `t`    | 5024  | 4    | 5   | 1256 blocks     | 매우 큰 스케일 — 기후대 크기의 온도 변화   |
| `h`    | 5048  | 4    | 5   | 1262 blocks     | 매우 큰 스케일 — 기후대 크기의 습도 변화   |
| `d`    | 24    | 2    | 4   | 12 blocks       | 아주 작은 스케일 — 블록 하나 단위의 랜덤   |

### 3.2 base 노이즈 시각화

`4096 × 4096` 블록 영역에서 각 노이즈를 스플라인/정규화까지 통과시킨 결과.

![base noise maps](../../../../tools/terrain_noise_maps.png)

- `c`: 대륙 단위의 큰 덩어리가 보이고 스플라인이 낮은 값을 바다 쪽으로 눌러줌
- `e`: 가장 부드러운 저주파. 대륙 전체가 통째로 침식되거나 융기됨
- `pv`: `abs(v)` 를 사용해 절대치가 큰 곳(± 양쪽 극단)이 산 정상/골이 됨
- `t`, `h`: 5000 블록 스케일이라 4096 블록 안에서도 몇 개의 기후 지대만 보임
- `d`: scale 24 로 픽셀 단위의 흰 잡음처럼 보임 → 블록 단위 랜덤 결정에 사용

### 3.3 스플라인 (`c` / `e` / `pv` 만)

`t`, `h`, `d` 는 `[-1,1]` → `[0,1]` 로만 정규화하지만
`c` / `e` / `pv` 는 지형 형태를 직접 결정하므로 **piecewise cubic-lerp 스플라인**으로
비선형 리매핑을 통과시킨다.
스플라인의 역할은 "특정 raw 노이즈 구간에서만 급격히 변하는" 계단형 응답을
매끄럽게 만드는 것이다.

![spline curves](../../../../tools/terrain_splines.png)

**`SplineContinentalness`** — 낮은 raw 값에서 오랫동안 낮게 눌러 두다가
0 근처부터 급격히 상승, 0.42 부근에서 이미 0.92 에 도달.
결과적으로 대륙과 해양이 명확히 분리되고 해안선은 얇게 남는다.

**`SplineErosion`** — 8개 구간으로 촘촘하게 나뉜 계단형 곡선. 침식은
지형 위에 곱해지므로 (`(1-e)`) 계단이 촘촘할수록 침식된 평지와 융기된 산의
경계가 뚜렷해진다.

**`SplinePeaksValley`** — 입력에 `abs(v * 1.5)` 를 씌워 대칭 M 형이 되고
마지막 구간에서만 다시 감소한다.
raw 값이 0 근처(평지) 이면 0.01, 양 극단(-0.66, +0.66 부근) 이면 1.0,
아주 극단 (±1) 이면 다시 조금 낮아진다.
그 뒤 `(pv - 0.5) * 2.0` 으로 `[-1, 1]` 로 재중심 시켜 elevation 계산에 들어간다.

### 3.4 c / e / pv 가 지형 높이에 미치는 영향

```cpp
// voxen/headers/Terrain.h
static float GetElevation(float c, float e, float pv)
{
    if (c <= 0.1f)
        c = c / 0.1f - 1.0f;         // [-1, 0]  → 바다 방향으로 눌림
    else
        c = (c - 0.1f) / 0.9f;       // [ 0,  1] → 육지 방향으로 융기

    float elevation = 64.0f * c * (1.0f - e)
                    + 64.0f * pv * powf((1.0f - e), 1.25f);

    return std::clamp(elevation, -128.0f, 128.0f);
}
```

두 항을 분리해서 해석:

- **`64 * c' * (1 - e)`** — **land / ocean base term**
  - `c'` 는 스플라인 후 c 를 다시 `[-1, 1]` 로 remap 한 값
  - `c'` 가 -1 에 가까울수록 (해양) 이 항은 -64 근처, +1 에 가까울수록 +64
  - `(1 - e)` 를 곱하므로 침식이 강할수록 이 항은 0 에 수렴 (완만해짐)
- **`64 * pv * (1 - e)^1.25`** — **mountain / valley term**
  - `pv` 값에 따라 정상 (+)/골 (-) 방향으로 높이를 얹거나 판다
  - `(1 - e)^1.25` 는 (1-e) 보다 더 급하게 침식에 반응해서, 강하게 침식된
    지역에서는 산 자체가 사라지고 평지만 남게 한다

두 항을 시각적으로 분해하면:

![elevation composition](../../../../tools/terrain_elevation.png)

- 좌하단 `64 * c' * (1-e)` : 대륙 (밝은 톤) 과 해양 (어두운 톤) 만 분리됨
- 중하단 `64 * pv * (1-e)^1.25` : 여기서 산맥/협곡의 요철이 얹힘
- 우하단 `Terrain::GetElevation` : 둘의 합. 이 값은 아직 world Y 가 아니라
  "표면 위 아래로 얼마나 밀 것인가" 의 오프셋 (`[-128, 128]` clamp)

### 3.5 elevation 노이즈는 biome 을 어떻게 사용하는가

`Chunk::InitTerrainNoises` 가 캐싱하는 `elevationNoises[x][z]` 는
`Biome::GetBiomeTerrainHeight` 의 출력이며, 이것이 **실제 지형 표면의 world Y**
가 된다. 구현은 다음과 같다.

```cpp
// voxen/srcs/Biome.cpp
float Biome::GetBiomeTerrainHeight(float c, float e, float pv, float t, float h)
{
    float sumBiomeBaseHeight = 0.0f;
    float sumElevationScale  = 0.0f;
    float sumWeight          = 0.0f;

    float coEffi[4] = { 10.0f, 1.0f, 5.0f, 5.0f };
    for (int i = 0; i < BIOME_COUNT; ++i) {
        const auto& p = GetWeightParams((BIOME_TYPE)i);

        float cDiff = coEffi[0] * std::powf(std::fabs(c - p.continentalness), 2.0f);
        float eDiff = coEffi[1] * std::powf(std::fabs(e - p.erosion),          2.0f);
        float tDiff = coEffi[2] * std::powf(std::fabs(t - p.temperature),      2.0f);
        float hDiff = coEffi[3] * std::powf(std::fabs(h - p.humidity),         2.0f);
        float diff  = max(1e-5f, std::sqrtf(cDiff + eDiff + tDiff + hDiff));

        float weight  = 1.0f / (diff * diff);
        float weight2 = std::powf(weight, 5.0f);           // 가장 가까운 biome 에 극단적 편중

        sumBiomeBaseHeight += weight2 * p.baseHeight;
        sumElevationScale  += weight2 * p.elevationScale;
        sumWeight          += weight2;
    }

    float biomeBaseHeight = sumBiomeBaseHeight / sumWeight;
    float elevationScale  = sumElevationScale  / sumWeight;
    float elevation       = Terrain::GetElevation(c, e, pv);

    return std::clamp(biomeBaseHeight + elevationScale * elevation, 1.0f, 255.0f);
}
```

이 식의 형태는 다음과 같이 해석할 수 있다.

```
height(x, z) = biomeBaseHeight(c, e, t, h)          // 이 지역의 biome 평균 기준 높이
             + elevationScale(c, e, t, h)           // biome 별로 얼마나 요철을 반영할지
               * Elevation(c, e, pv)                // 형태만 담당하는 순수 지형 노이즈
```

- **base + scale * noise** 의 표준 지형 합성 패턴이다.
- biome 파라미터 (`baseHeight`, `elevationScale`) 는 각 biome 이 얼마나 높은 곳에서
  시작하는지 / 요철에 얼마나 민감한지를 갖고, 실제 좌표에서는 **가장 가까운 biome
  파라미터들의 가중 평균** 이 쓰인다.
- 가중치를 `1 / diff^2` 의 5제곱까지 밀어 놓아서 실제로는 최근접 biome 값이 지배적이고
  경계 지역에서만 부드럽게 섞이는 결과가 된다.

biome 별 파라미터 (`voxen/headers/Biome.h::BiomeTypeInfoSet`) 예시:

| biome        | c    | e   | t     | h     | scale | base |
| ------------ | ---- | --- | ----- | ----- | ----- | ---- |
| OCEAN        | 0.0  | 0.5 | 0.5   | 0.5   | 0.025 | 32   |
| PLAINS       | 0.7  | 0.5 | 0.435 | 0.16  | 0.025 | 64   |
| FOREST       | 0.7  | 0.5 | 0.53  | 0.66  | 1.5   | 64   |
| DESERT       | 0.7  | 0.5 | 0.81  | 0.125 | 0.01  | 80   |
| TUNDRA       | 0.7  | 0.5 | 0.125 | 0.2   | 0.025 | 108  |
| SNOWY_TAIGA  | 0.7  | 0.5 | 0.28  | 0.66  | 0.75  | 96   |

즉 같은 `Elevation(c, e, pv)` 이 나와도

- OCEAN 은 `base 32 + scale 0.025 * elevation` → 거의 항상 얕은 물 밑
- PLAINS 는 `base 64 + scale 0.025 * elevation` → 평탄한 초원 (거의 base 값만)
- FOREST 는 `base 64 + scale 1.5 * elevation` → elevation 요철이 그대로 지형에 반영
- TUNDRA 는 `base 108 + scale 0.025 * elevation` → 평탄한 고지대

라는 서로 다른 지형이 만들어진다.

biome 결정과 최종 지형 높이 (수면 = world Y 63, cyan 등고선):

![biome map & height](../../../../tools/terrain_height.png)

## 4. 구현 내용

### 4.1 캐시 자료구조

노이즈는 청크 로딩용 임시 메모리 (`ChunkLoadMemory`) 에 저장된다.
이 구조는 청크 풀과 별도로 존재하는 스레드 로컬 버퍼로,
로드가 끝나면 재사용된다.

```cpp
// voxen/headers/Chunk.h
struct ChunkLoadMemory {
    ...
    float continentalinessNoises[Chunk::CHUNK_SIZE_P][Chunk::CHUNK_SIZE_P];
    float erosionNoises       [Chunk::CHUNK_SIZE_P][Chunk::CHUNK_SIZE_P];
    float peaksValleyNoises   [Chunk::CHUNK_SIZE_P][Chunk::CHUNK_SIZE_P];
    float temperatureNoises   [Chunk::CHUNK_SIZE_P][Chunk::CHUNK_SIZE_P];
    float humidityNoises      [Chunk::CHUNK_SIZE_P][Chunk::CHUNK_SIZE_P];
    float distributionNoises  [Chunk::CHUNK_SIZE_P][Chunk::CHUNK_SIZE_P];
    float elevationNoises     [Chunk::CHUNK_SIZE_P][Chunk::CHUNK_SIZE_P];
    ...
};
```

`CHUNK_SIZE_P = CHUNK_SIZE + 2 = 34` 로 청크 양쪽에 padding 1 이 붙는 이유는
Greedy Meshing / 인접 블록 참조 시 옆 청크의 값을 참조해야 하기 때문이다.

### 4.2 world 좌표 계산

`InitTerrainNoises` 는 로컬 인덱스 `x, z ∈ [0, 33]` 을
`worldX = offset.x + x - 1` 형태로 변환해 노이즈 함수에 넣는다.
`- 1` 이 붙는 이유는 로컬 index 0 이 offset 의 -1 블록에 해당하는 padding 이기 때문이다.
결과적으로 두 인접한 청크는 자신들의 겹치는 좌표에서 **같은 world 값을 다시 뽑으므로
경계에서 자연스럽게 이어지는** 지형이 나온다.

### 4.3 base 노이즈 6종의 사용처

| noise | biome 결정 | elevation | block 타입 | tree/instance | block 색상 |
| ----- | :--------: | :-------: | :--------: | :-----------: | :--------: |
| `c`   |     O      |     O     |     O      |               |            |
| `e`   |     O      |     O     |     O      |               |            |
| `pv`  |            |     O     |     O      |               |            |
| `t`   |     O      |     O     |     O      |               |     O      |
| `h`   |     O      |     O     |     O      |               |     O      |
| `d`   |            |           |     O      |       O       |            |

- biome 결정과 elevation 계산은 c/e/t/h (지역 특성) 만 사용.
  `pv` 는 elevation 안에서 지형 요철에만 관여한다.
- `d` 는 blocktype/instance 결정 단계에서 "같은 조건에서 어느 variant 을 고를지"
  를 결정하는 hash-like 노이즈로만 쓰이므로 상관성이 아주 약해야 한다 (scale 24).

## 5. 문제점 & 해결

### 5.1 최근접 biome 만 잡으면 경계가 불연속

경계에서 서로 다른 biome 이 만나면 `baseHeight` 가 32(OCEAN) → 108(TUNDRA)
처럼 불연속하게 바뀌어 지형에 절벽이 생긴다.

**해결** : `Biome::GetBiomeTerrainHeight` 를 argmin (하드 스위치) 이 아닌
가중 평균으로 구성. 다만 완전한 부드러움은 원치 않아서 `weight^5` 로 지수를 크게 두어
경계에서만 살짝 섞이고 내부는 여전히 최근접 biome 이 지배하도록 했다.

### 5.2 축이 서로 상관되면 조합 폭이 좁아진다

`t`, `h`, `d` 를 다 같은 파라미터 (같은 scale/seed) 로 뽑으면 사실상 한 축이 되어
biome 다양성이 사라진다.

**해결** : 각 노이즈마다 서로 다른 `seed` 를 좌표에 더해 완전히 다른 sampling
공간을 사용하도록 구성 (`e=+123`, `pv=+4`, `t=+653`, `h=+157`, `d=+773`).

### 5.3 노이즈 값을 매 프레임 다시 뽑으면 비싸다

`GetBlockType` 안에서 c/e/pv/t/h/d 를 그때그때 뽑도록 하면 CHUNK_SIZE_P^3 (약 4만 회)
호출이 매 chunk 마다 발생한다. Perlin FBM 은 옥타브 * hash 비용이 크므로 이 방식은 성능 병목이 될 수 있다.

**해결** : `InitTerrainNoises()` 로 (x, z) 2D 노이즈를 **한 번만** 뽑아
`ChunkLoadMemory` 에 캐싱. Y 축 순회는 캐시된 값을 참조.
2D * 34^2 = 1156 회 sampling 으로 축소된다.

### 5.4 `pv` 스플라인의 이산 계단감

`SplinePeaksValley` 는 abs 를 취한 뒤 6개 구간으로 계단을 만드는데,
스플라인 구간 폭이 좁은 곳(0.36 ~ 0.42) 에서 시각적으로 계단감이 나타날 수 있다.

**현재 상태** : Cubic-Lerp 로 인접 구간 사이는 매끄럽지만
구간 폭이 좁은 곳은 여전히 관측된다. 필요 시 추후 스플라인 노드를 재조정할 여지가 있다.

## 6. 회고

- **base + scale * noise 패턴이 지형 합성의 표준** 이라는 것을
  구현하면서 체감했다. 처음에는 heightmap 노이즈 하나로 시도했지만 결과가
  단조롭고 biome 별 특징을 담을 수 없었다. 축을 나눈 뒤 각각의 역할을 명확히 하고
  마지막에 `baseHeight + elevationScale * elevation` 으로 합성하는 구조는
  **각 축이 하나의 관심사만 담당**하므로 튜닝이 훨씬 편했다.

- **스플라인은 코드가 길지만 튜닝 자산으로 봐야 한다.**
  `SplineContinentalness` 는 6개 구간, `SplineErosion` 은 8개 구간이라 파라미터가
  많아 보이지만, 이 값들이 결국 "우리 세계에서 해안선은 얼마나 얇을지",
  "침식된 평지가 얼마나 넓게 깔릴지" 를 결정한다. 이 스플라인 노드를 조정하는 것이
  Perlin 파라미터 (scale/freq) 를 바꾸는 것보다 훨씬 감이 잘 잡혔다.

- **아쉬운 점** : `elevationNoises` 라는 이름이 오해를 부른다. 실제로 저장되는 값은
  `Biome::GetBiomeTerrainHeight` 의 리턴이라 **elevation offset 이 아니라 최종 world Y**
  이다. `terrainHeightMap` 같은 이름이었으면 다른 서브시스템에서 이 값을 읽을 때
  더 직관적이었을 것.

- **개선하고 싶은 방향** : 현재 caching 은 2D 만 하고 3D 노이즈 (cave 등) 는
  매번 새로 뽑는다. `Terrain::IsCave` 는 y 축 위쪽으로 CHUNK_SIZE 만큼 반복 호출되므로
  이 부분도 3D 캐시가 있으면 로딩 속도가 더 빨라질 여지가 있다.
