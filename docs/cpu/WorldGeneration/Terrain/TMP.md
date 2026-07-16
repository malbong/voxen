# Terrain Generation

<img width="1914" height="1071" alt="Image" src="https://github.com/user-attachments/assets/15c75fb9-5c7a-48bb-b047-a001d16581e4" />

<img width="1918" height="1074" alt="Image" src="https://github.com/user-attachments/assets/ed9ae7e6-f5c4-4ac2-b112-8cee3825a178" />

## 1. 개요

높이 결정에 관련된 내용을 중심으로 이야기합니다: 블록 타입 결정은 Block 문서에서.

오픈월드 기반 절차적 지형 생성을 위해 동적으로 여러 노이즈를 결합하여 biome, block type, tree/instance 배치, 높이 등을 결정한다.
노이즈 값은 청크 초기화(로딩)시 미리 뽑아 저장해두고 필요 시 연산 결과를 계산한다.

`InitTerrainNoises()`는 이 노이즈를 채우는 함수이며, 이후
`InitBiomeMapAndCount → InitBasicBlockType → InitTreePlace → InitInstancePlace → InitWorldVerticesData`에서 모두 이 노이즈 캐시에 의존하여 사용한다.

노이즈 별 역할은 아래와 같다.

| 노이즈            | 역할                                                      | 최종 사용처                    |
| ----------------- | --------------------------------------------------------- | ------------------------------ |
| `continentalness` | 대륙성. 높으면 기본 높이가 오름                           | elevation, biome 가중치        |
| `erosion`         | 침식. `c` 와 `pv` 를 눌러 평탄화                          | elevation, biome 가중치        |
| `peaks-valley`    | 뾰족한 산 ↔ 협곡. 지형에 요철을 얹음                      | elevation                      |
| `temperature`     | 온도. 색 결정, 바이옴 및 블록 타입 결정                   | biome 가중치, block color/type |
| `humidity`        | 습도. 색 결정, 바이옴 및 블록 타입 결정                   | biome 가중치, block color/type |
| `distribution`    | 분산. 블록/인스턴스의 마지막 선택 시 결정 노이즈로 사용   | tree/instance/block variant    |
| `elevation`       | xz 기준 최종 지형 높이 (world Y) - 바이옴타입에 의해 변경 | 지형 표면, 트리 배치 기준 등   |

## 2. 도입 동기

[마인크래프트 절차적 지형 생성 유튜브](https://www.youtube.com/watch?v=YyVAaJqYAfE)

원래는 단순히 heightMap 하나를 Perlin 노이즈 하나로 height를 최종 결정했다.

지형이 마음에 들지 않으면 이것저것 시도해봐도 큰 변화는 보이지 않았고, 수정한다한들 의미가 존재하지 않아 매번 값을 수정하며 재실행했다.

이러한 비효율적인 상황에서 절차적 지형생성에 관련된 문서 및 동영상을 찾아보게 되었고, 여러 노이즈를 합성하여 의미를 두고 연산 결과에 사용하는 모습이 존재했다.

## 3. 구현 내용

### 3.1 Perlin FBM 의 `scale` / `freq` / `octave`

```cpp

// 노이즈 호출: Terrain.h
static float GetContinentalness(int x, int z)
{
    float scale = 1024.0f;
    float cNoise = Utils::PerlinFbm(x / scale, z / scale, 2.0f, 6);
    ...
}

// 노이즈 생성: Utils.h
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

여기서 각 파라미터의 의미:

- **`scale`** : 진동수와 무관하게 큰 한칸의 크기다. scale 값으로 압축한다. `[0, scale] -> [0, 1]`로 압축하게 된다.
- **`freq`** : scale로 나뉜 값이 진동수 값으로 의해 나뉜다. Perlin 내부에서 정수 격자를 잡으므로, 원래의 큰 셀 한 칸이 freq로 의해 한번 더 나뉘여 내부 연산을 하게 된다.
- **`octave`** : 매 반복마다 `freq *= 2` (격자 폭 절반) 와 `amp *= 2^-0.85` (기여도 감소)를 적용해 노이즈를 겹친다. 옥타브를 늘리면 진동수가 올라가고 세부한 정도가 올라간다.

예시 `scale = 1024, freq = 2, octave = 4` 일 때 각 옥타브가 world 를 어떻게 쪼개는지:

<img width="2222" height="650" alt="Image" src="https://github.com/user-attachments/assets/debe0e5b-1e94-4a29-b320-6a0692a4b394" />

이 관점에서 6개 노이즈의 특성을 정리하면:

| 노이즈 | scale | freq | oct | 첫 옥타브 셀 폭 | 특징                                          |
| ------ | ----: | ---: | --: | --------------: | --------------------------------------------- |
| `c`    |  1024 |    2 |   6 |      512 blocks | 대륙/해양 규모의 큰 덩어리                    |
| `e`    |  2048 |    2 |   4 |     1024 blocks | 가장 큰 스케일 — 대륙 전체를 덮는 완만한 변화 |
| `pv`   |   512 |  1.5 |   6 |      341 blocks | 산맥/협곡 규모 — 6옥타브로 세부까지 침투      |
| `t`    |  5024 |    4 |   5 |     1256 blocks | 매우 큰 스케일 — 기후대 크기의 온도 변화      |
| `h`    |  5048 |    4 |   5 |     1262 blocks | 매우 큰 스케일 — 기후대 크기의 습도 변화      |
| `d`    |    24 |    2 |   4 |       12 blocks | 아주 작은 스케일 — 블록 하나 단위의 랜덤      |

<img width="2456" height="1528" alt="Image" src="https://github.com/user-attachments/assets/8b81642b-7301-4302-a714-5d31085ec623" />

### 3.2 Perlin 노이즈

격자를 구성으로 각 격자 모서리에 랜덤한 4개의 벡터를 가진다.

해당 모서리에서 임의의 점(x, y)을 향하는 벡터와 각 모서리가 가지는 벡터를 서로 내적한 결과를 가지고 Bilnear Interpolation 하여 결과를 얻는다.

랜덤난수와 달리, 이는 주변에 대해 부드럽게 노이즈를 구성할 수 있다.

추가적으로 난수 생성, 보간 방식 등에 따라 결과는 달라진다.

```cpp
// PerlinNoise: Utils.h
static float GetPerlinNoiseFbm(float x, float y)
{
	Vector2 p = Vector2(x, y);
	int x0 = (int)floor(x);
	int x1 = x0 + 1;
	int y0 = (int)floor(y);
	int y1 = y0 + 1;

	float n0 = Hash(x0, y0).Dot(p - Vector2((float)x0, (float)y0));
	float n1 = Hash(x1, y0).Dot(p - Vector2((float)x1, (float)y0));
	float n2 = Hash(x0, y1).Dot(p - Vector2((float)x0, (float)y1));
	float n3 = Hash(x1, y1).Dot(p - Vector2((float)x1, (float)y1));

	float i0 = CubicLerp(n0, n1, p.x - (float)x0);
	float i1 = CubicLerp(n2, n3, p.x - (float)x0);

	return CubicLerp(i0, i1, p.y - (float)y0);
}
```

### 3.3 Spline (`c` / `e` / `pv` 만)

참고한 비디오에선 극적인 변화나, 값을 튜닝하고 싶으면 스플라인 점을 이용할 수 있다고 하였다.

그래서 지형 전반적인 결과에 영향을 주는 c, e, pv 값만 Spline을 통해 값을 튜닝하게 되었다.

결과를 보고 만족할 때까지 스플라인의 점들의 위치를 적절히 바꿔가며 사용했다.

<img width="2501" height="719" alt="Image" src="https://github.com/user-attachments/assets/7a6d8d2c-57a2-426a-82a3-0969306d7ca2" />

### 3.4 c / e / pv 가 지형 높이에 미치는 영향

```cpp
// voxen/headers/Terrain.h
static float GetElevation(float c, float e, float pv)
{
    if (c <= 0.1f)
        c = c / 0.1f - 1.0f;         // [-1, 0]  → 바다 방향으로 눌림
    else
        c = (c - 0.1f) / 0.9f;       // [ 0,  1] → 육지 방향으로 융기

    float elevation = 64.0f * c * (1.0f - e) + 64.0f * pv * powf((1.0f - e), 1.25f);

    return std::clamp(elevation, -128.0f, 128.0f);
}
```

해석:

- **`c` 범위 재정의**
  - 스플라인 함수에서 생각했던 값을 음수 범위로 재조정
  - 음수는 바다, 양수는 육지로 판단했다.
- **`64 * c' * (1 - e)`** — 해양과 육지의 기본 높이
  - 물 표면의 높이는 `64`로 고정되어있다. `c`값이 높으면 육지와 가깝게, 그렇지 않으면 음수로 바다쪽으로 간다.
  - 이 때, `(1 - e)` 값을 사용하는데, 침식이 높으면 기본 높이를 줄인다.
- **`64 * pv * (1 - e)^1.25`** — 얹거나 파기
  - `pv` 값에 따라 정상 (+)/골 (-) 방향으로 높이를 얹거나 판다
  - `(1 - e)^1.25` 는 (1-e) 보다 더 급하게 침식에 반응해서, 강하게 침식되게 한다.

### 3.5 최종 높이 결정은?

사실 위의 `elevation`은 최종 높이가 아니며, 실제로 최종 높이는 Biome에 따라 높낮이가 변경된다.

이렇게 진행한 이유는 Biome이 기본적으로 가져야할 높이가 존재해야 월드 구성이 훨씬 자연스러웠기 때문이다.

그래서 최종 높이 결정은 Biome에 달려있다.

- `Biome::baseHeight`: Biome의 평균 높이
- `Biome::elevationScale`: Biome이 elevation 노이즈에 얼마나 영향을 받을 것인가

이 두 개의 Biome 값을 가지고 최종 높이를 결정한다.

- 최종 높이: `biomeBaseHeight + elevationScale * elevation`

이 때, 단순히 해당 블록의 위치의 Biome을 결정하고 Biome에 대한 값을 하드하게 두면 바이옴 경계에서 Sharp하게 높이 구분이 되어버리는 문제가 발생한다.
그 결과 블록 위치의 Biome 가중치를 결정하고 가중치에 따라 높이를 결정하여 부드럽게 처리한다.

```cpp
// voxen/srcs/Biome.cpp
float Biome::GetBiomeTerrainHeight(float c, float e, float pv, float t, float h)
{
    float sumBiomeBaseHeight = 0.0f;
    float sumElevationScale  = 0.0f;
    float sumWeight          = 0.0f;

    // 가중치 계산
    float coEffi[4] = { 10.0f, 1.0f, 5.0f, 5.0f }; // 가중치 계수
    for (int i = 0; i < BIOME_COUNT; ++i) {
        const auto& p = GetWeightParams((BIOME_TYPE)i);

        float cDiff = coEffi[0] * std::powf(std::fabs(c - p.continentalness), 2.0f);
        float eDiff = coEffi[1] * std::powf(std::fabs(e - p.erosion),          2.0f);
        float tDiff = coEffi[2] * std::powf(std::fabs(t - p.temperature),      2.0f);
        float hDiff = coEffi[3] * std::powf(std::fabs(h - p.humidity),         2.0f);

        // 값이 작을 수록, 특정 바이옴에 편중됨
        float diff  = max(1e-5f, std::sqrtf(cDiff + eDiff + tDiff + hDiff));

        // 역수 계산
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

### 3.6 시각화

눈으로 덮힌 부분은 높이가 비교적 높고, 사막 부분은 높이가 비교적 낮은 걸 볼 수 있다.

<img width="2126" height="961" alt="Image" src="https://github.com/user-attachments/assets/99e7d227-9eb2-4e70-b34e-95a1461492d8" />

## 4. 문제점 & 해결

### 4.1 최근접 biome 만 잡으면 경계가 불연속

경계에서 서로 다른 biome 이 만나면 `baseHeight` 가 `32(OCEAN) → 108(TUNDRA)` 처럼 불연속하게 바뀌어 지형에 절벽이 생긴다.

**해결** : `Biome::GetBiomeTerrainHeight` 를 하드 스위치가 아닌
가중 평균으로 구성. 다만 완전한 부드러움은 원치 않아서 `weight^5` 로 지수를 크게 두어
경계에서만 살짝 섞이고 내부는 여전히 최근접 biome 이 지배하도록 했다.

### 4.2 scale 동일하여 노이즈 구성이 같은 문제

`t`, `h`, `d` 를 다 같은 파라미터 (같은 scale) 로 뽑으면 사실상 한 축이 되어 biome 다양성이 사라진다.

**해결** : 각 노이즈마다 서로 다른 `seed` 를 좌표에 더해 완전히 다른 공간을 사용하도록 구성, 값은 하드하게 결정

### 4.3 노이즈를 필요 시 호출

노이즈를 필요 시 호출하는 경우 34x34x34 경우에 대해 모두 노이즈 연산을 진행하여 비용 부담이 크다.

**해결**: `ChunkLoadMemory`에 두고 캐싱하여 사용한다. 추가로 노이즈는 대부분 2D 기반으로 정의한다.

## 5. 회고

### xz 평면에 대한 노이즈 재활용에 대한 고찰

- 노이즈를 xz-2D로 구성하다보니, 같은 xz 위치에 존재하는 청크에 대해서는 노이즈가 모두 같은 결과라 재활용을 고민했다.
- 청크를 32 x 32 x 32가 아닌 32 x MAX x 32 로 구성할까도 고민했지만 청크 수정에 대해서 비용이 클 것 같았다.
- 노이즈 결과를 임시 캐싱해놓고 사용할까?: 캐시 추가/삭제가 공유메모리였고, 뮤텍스로 언락/락으로 제어하니 속도 변화는 드라마틱하지 않아 삭제했다.
- 그럼 같은 xz를 모아둔 청크들을 담는 부모의 형태로 저장할까?: 구조가 변경될 필요가 존재했고, 현재 청크 로드 자체가 불편함이 없어 현재 코드를 유지했다. 물론 성능 개선이 필요하다면 어렵지 않게 해결할 수 있는 문제.

### c, e, pv와 Biome에 대한 높이 결정

- Biome 별로 높이가 다를 필요가 꼭 존재해야한다고 생각했다.
- 그럼 Biome이 높이에 영향을 주는 것인가, 높이가 Biome에 영향을 주는 것인가에 대한 고민도 했다.
- 높이에 따라 바이옴이 결정된다면 같은 xz에 다른 높이에 있는 임의의 두 블록이 바이옴이 다를 수 있고, 하드하게 나눈다면 딱딱해보였다.
- 그렇다고 하드하게 나누지 않기에는 3D 노이즈가 필요했다고 판단하여 바이옴이 전체의 높이에 관여하게끔 결정하게 되었다.

### 노이즈 속도 개선 필요

- 절대적으로 노이즈 자체를 많이 호출있기에 성능 개선이 필요하다면 살펴봐야하는 곳이다.

### Spline 값 수정

- 값을 수정하는 일이 굉장히 많았고, 이게 파라미터 튜닝인가 싶은 빌드 실행의 반복이였다.
- 효과적으로 하는 방법은 떠올리지 못했다.
