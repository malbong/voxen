# Dynamic Skybox — 셰이더 기반 절차적 하늘 렌더링

## 1. 개요

큐브맵 텍스처 없이, **셰이더만으로** 동적 하늘을 렌더링하는 시스템이다. CPU에서 게임 내 시간에 따라 하늘 색상 4종을 결정하여 Constant Buffer로 전달하고, Pixel Shader가 픽셀의 방향(고도, 태양 방향과의 각도)에 따라 이 색상들을 혼합하여 최종 하늘색을 만든다. 이 하늘 색상은 Skybox 렌더링뿐 아니라 씬 전체의 간접 조명(Ambient Light) 색상에도 동일하게 반영된다.

## 2. 도입 동기

정적 큐브맵을 사용하면 하늘은 항상 고정된 이미지다. 낮/밤 전환, 일출/일몰의 색 변화를 표현하려면 여러 큐브맵을 미리 제작하고 블렌딩해야 하는데, 이는 에셋 제작 비용이 크고 전환이 딱딱하다.

셰이더에서 절차적으로 색을 결정하면:
- 큐브맵 에셋이 필요 없다
- 시간에 따른 색 전환이 연속적이고 자연스럽다
- 하늘 색상을 간접 조명에 직접 재사용할 수 있어 일관된 톤을 유지한다

## 3. 핵심 아이디어

하늘의 색은 두 가지 축으로 결정된다:

1. **고도(Altitude)** — 수평선(Horizon)에서 천정(Zenith)으로 갈수록 색이 변한다
2. **태양 방향(Sun Direction)** — 태양을 바라보는 방향에서는 태양 색이 강해지고, 반대 방향에서는 일반 하늘 색이 된다

CPU는 현재 시간에 따라 4개의 색상(Normal Horizon/Zenith, Sun Horizon/Zenith)을 계산하여 GPU에 전달하고, PS가 이 두 축을 조합하여 최종 색상을 결정한다.

```
                  Zenith (천정)
                     ↑
   normalZenith ←────┤────→ sunZenith
                     │
                     │ (고도에 따른 보간)
                     │
   normalHorizon ←───┤───→ sunHorizon
                     ↓
                  Horizon (수평선)

            ←── 반태양 방향  |  태양 방향 ──→
                (sunDirWeight에 따른 보간)
```

## 4. 구현 내용

### 4.1 시간 체계 (Date.h)

하루는 24000 tick이며, 실시간 30초에 대응한다.

| 구간 | Tick 범위 | 설명 |
|---|---|---|
| Day | 1000 ~ 11000 | 낮 (색상 고정) |
| Day → Sunset | 11000 ~ 12500 | 석양 색 강화 |
| Sunset → Night | 12500 ~ 13700 | 석양 색 소멸 |
| Night | 13700 ~ 22300 | 밤 (색상 고정) |
| Night → Sunrise | 22300 ~ 23500 | 일출 색 강화 |
| Sunrise → Day | 23500 ~ 25000(=1000) | 일출 색 소멸 |

### 4.2 CPU: 시간 기반 색상 결정 (Skybox.cpp)

`Update(dateTime)`에서 현재 tick에 따라 4개의 색상 벡터를 결정한다.

#### 색상 프리셋 (Skybox.h, sRGB)

| 색상 | Horizon | Zenith |
|---|---|---|
| **Normal Day** | (0.67, 0.82, 1.0) 밝은 하늘색 | (0.52, 0.67, 1.0) 진한 파랑 |
| **Normal Night** | (0.19, 0.20, 0.24) 어두운 회청색 | (0.17, 0.175, 0.195) 더 어두운 남색 |
| **Sun Day** | (0.60, 0.74, 1.0) 연한 파랑 | (0.32, 0.45, 1.0) 깊은 파랑 |
| **Sun Sunrise** | (0.72, 0.60, 0.34) 따뜻한 주황 | SunDay↔Night 보간 |
| **Sun Sunset** | (0.64, 0.26, 0.04) 붉은 주황 | SunDay↔Night 보간 |

#### 보간 방식

**Day/Night 고정 구간**에서는 프리셋 그대로 사용하고, **전환 구간**에서는 선형 보간(Lerp)으로 부드럽게 전환한다.

```cpp
// Skybox.cpp — 석양 전환 예시
if (Date::DAY_END <= dateTime && dateTime < Date::MAX_SUNSET) {
    float w = (float)(dateTime - Date::DAY_END) / (Date::MAX_SUNSET - Date::DAY_END);
    sunHorizonColor = Utils::Lerp(SUN_DAY_HORIZON, SUN_SUNSET_HORIZON, w);
    sunZenithColor  = Utils::Lerp(SUN_DAY_ZENITH,  SUN_SUNSET_ZENITH,  w);
}
```

Normal 색(일반 방향)과 Sun 색(태양 방향)은 독립적인 전환 구간을 가진다:
- **Normal 색**: Day ↔ Night 사이를 단순하게 보간
- **Sun 색**: Day → Sunset → Night → Sunrise → Day로 4단계 전환, 일출/일몰 색을 별도로 거침

최종 4색은 `sRGB → Linear` 변환 후 `SkyboxConstantBuffer (b9)`로 GPU에 전달한다.

```cpp
m_constantData.normalHorizonColor = Utils::SRGB2Linear(normalHorizonColor);
m_constantData.normalZenithColor  = Utils::SRGB2Linear(normalZenithColor);
m_constantData.sunHorizonColor    = Utils::SRGB2Linear(sunHorizonColor);
m_constantData.sunZenithColor     = Utils::SRGB2Linear(sunZenithColor);
```

### 4.3 Vertex Shader: 방향 벡터 전달 (SkyboxVS.hlsl)

스카이박스 메시는 카메라를 감싸는 구(sphere)이며, 인덱스 순서를 반전(`std::reverse`)하여 안쪽 면이 Front Face가 되도록 한다.

VS에서 핵심은 **카메라 위치를 무시하고 방향만 전달**하는 것이다:

```hlsl
output.posProj = mul(float4(output.posProj.xyz, 0.0), view);  // w=0 → 이동 제거
output.posProj = mul(float4(output.posProj.xyz, 1.0), proj);  // w=1 → 투영 적용
```

View 변환 시 `w=0`으로 곱하면 Translation이 무시되어, 카메라가 어디에 있든 스카이박스가 항상 카메라 중심에 위치하게 된다. `posWorld`에는 변환 전 원본 정점 위치가 그대로 전달되어, PS에서 방향 벡터로 사용된다.

### 4.4 Pixel Shader: 하늘 색상 결정 (SkyboxPS.hlsl)

#### 4.4.1 태양/달 렌더링과 getPlanetTexcoord TBN 변환

태양과 달은 `getPlanetTexcoord()`로 스카이박스 구면 위에 텍스처를 투영한다. 이 함수의 핵심은 **3D 구면 방향을 천체 표면의 2D 텍스처 좌표로 변환**하는 것이며, 이를 위해 TBN 좌표계 변환을 사용한다.

```hlsl
bool getPlanetTexcoord(float3 posDir, float3 planetDir, float size, out float2 texcoord)
{
    float PDotP = dot(planetDir, posDir);
    float3 v = normalize(posDir - PDotP * planetDir);
    float3 p = v * tan(acos(PDotP));

    if (PDotP > 0.0 && length(p) < size)
    {
        float3 N = -planetDir;
        float3 T = float3(cos(PI / 4.0), 0.0, -cos(PI / 4.0));
        float3 B = cross(N, T);
        float3x3 TBNMatrix = float3x3(T, B, N);

        float3 vTBN = mul(p, transpose(TBNMatrix));

        texcoord.x = 0.5 + vTBN.x * (0.5 / size);
        texcoord.y = 0.5 + vTBN.y * (0.5 / size);
        ret = true;
    }
    ...
}
```

이 변환은 3단계로 진행된다:

**1단계: 접선 평면(Tangent Plane) 위의 위치 계산**

```
posDir ───────→ ·  (스카이박스 위 픽셀 방향)
                /|
               / |
              /  |  p = v * tan(θ)
             /   |
planetDir → · ───┘  (천체 중심 방향)
            θ = acos(dot(planetDir, posDir))
```

- `PDotP = dot(planetDir, posDir)` — 천체 방향과 픽셀 방향의 코사인 (= cos θ)
- `v = normalize(posDir - PDotP * planetDir)` — `posDir`에서 `planetDir` 성분을 빼면 수직 성분만 남는다. 이를 정규화하면 천체 중심에서 픽셀 방향으로의 단위 벡터가 된다
- `p = v * tan(acos(PDotP))` — `tan(θ)`를 곱하면 천체 방향을 법선으로 하는 접선 평면 위의 실제 위치가 된다. 이는 중심 투영(gnomonic projection)으로, 구면 위의 방향을 접선 평면 위의 점으로 변환한다

`PDotP > 0`이면 픽셀이 천체와 같은 반구에 있고, `length(p) < size`이면 천체 디스크 안에 들어온다.

**2단계: TBN 좌표계 구축**

접선 평면 위의 점 `p`는 3D 월드 좌표이므로, 이를 텍스처의 2D 좌표로 변환하려면 접선 평면 위의 로컬 좌표축이 필요하다.

- `N = -planetDir` — 접선 평면의 법선. 천체 방향의 반대(카메라를 향하는 방향)
- `T = float3(cos(π/4), 0, -cos(π/4))` = `(√2/2, 0, -√2/2)` — 고정된 접선 방향. XZ 평면에서 45° 방향으로, 태양/달 텍스처가 항상 일관된 회전 각도를 유지하게 한다
- `B = cross(N, T)` — 법선과 접선의 외적으로 종접선(Bitangent)을 구한다

이 세 벡터가 접선 평면 위의 직교 좌표계(TBN)를 형성한다.

**3단계: 월드 → TBN 좌표 변환**

```hlsl
float3x3 TBNMatrix = float3x3(T, B, N);  // 행: T, B, N
float3 vTBN = mul(p, transpose(TBNMatrix));
```

`float3x3(T, B, N)`은 T, B, N이 각각 행(row)인 행렬이다. 이 행렬의 전치(transpose)를 오른쪽에 곱하면:

```
vTBN = (dot(p, T), dot(p, B), dot(p, N))
```

즉, 월드 좌표 `p`를 T, B, N 축에 투영(내적)하여 TBN 공간의 좌표로 변환한다. `vTBN.x`는 접선 방향 성분, `vTBN.y`는 종접선 방향 성분이 되어 텍스처의 U, V에 대응한다.

최종적으로 `[-size, +size]` 범위를 `[0, 1]` UV로 매핑한다:

```hlsl
texcoord.x = 0.5 + vTBN.x * (0.5 / size);
texcoord.y = 0.5 + vTBN.y * (0.5 / size);
```

#### 태양/달 크기와 밝기

```hlsl
float sunSize = lerp(0.25, 0.6, pow(max(dot(lightDir, eyeDir), 0.0), 3.0));
```

태양 크기는 카메라가 태양을 바라볼수록 커진다(0.25 → 0.6). `pow(..., 3.0)`으로 비선형적으로 확대하여, 직접 마주볼 때만 뚜렷하게 커지는 효과를 만든다.

달은 `days % 8`로 8단계 월상(lunar phase)을 표현한다. 달 텍스처 아틀라스(4×2)에서 해당 위상의 서브 텍스처를 선택한다.

```hlsl
uint index = days % 8;
uint2 indexUV = uint2(index % col, index / col);
moonTexcoord += indexUV;
moonTexcoord = float2(moonTexcoord.x / col, moonTexcoord.y / row);
```

달의 밝기는 태양 밝기의 반비례로 결정된다:

```hlsl
float moonRadianceWeight = (maxRadianceWeight - radianceWeight) / maxRadianceWeight;
```

태양이 밝은 낮에는 달이 거의 보이지 않고, 밤에는 달이 밝아진다.

#### 4.4.2 배경 하늘 색상 결정

하늘색은 두 단계의 Lerp로 결정된다.

**1단계: 태양 방향에 따른 색상 선택**

```hlsl
float sunDirWeight = max(dot(lightDir, eyeDir), 0.0);
float3 horizonColor = lerp(normalHorizonColor, sunHorizonColor, sunDirWeight);
float3 zenithColor  = lerp(normalZenithColor,  sunZenithColor,  sunDirWeight);
```

`sunDirWeight`는 현재 픽셀 방향이 태양과 얼마나 일치하는지를 나타낸다. 태양을 바라보면 1에 가까워지며 Sun 계열 색상이, 반대 방향에서는 0에 가까워지며 Normal 계열 색상이 된다. 이로써 **석양 때 태양 방향만 붉게 물드는** 효과가 자연스럽게 발생한다.

**2단계: 고도에 따른 Horizon-Zenith 보간**

```hlsl
float posAltitude = sin(posDir.y);
float3 mixColor = (horizonColor + zenithColor) * 0.5;
float horizonAltitude = PI / 24.0;

if (posAltitude <= horizonAltitude)
    color += lerp(horizonColor, mixColor, pow((posAltitude + 1.0) / (1.0 + horizonAltitude), 15.0));
else
    color += lerp(mixColor, zenithColor, pow((posAltitude - horizonAltitude) / (1.0 - horizonAltitude), 0.5));
```

중간 색(`mixColor`)을 경계로 두 구간을 나누어 보간한다:

- **수평선 이하** (`posAltitude ≤ π/24`): `horizonColor → mixColor`로 전환. `pow(..., 15.0)`으로 매우 가파르게 보간하여, 수평선 근처에서만 Horizon 색이 유지되고 조금만 올라가도 빠르게 Mix 색으로 전환된다. 이로써 수평선 라인이 얇고 선명해진다.

- **수평선 이상** (`posAltitude > π/24`): `mixColor → zenithColor`로 전환. `pow(..., 0.5)`(= 제곱근)으로 완만하게 보간하여, 하늘 대부분이 부드러운 그라데이션을 형성한다.

### 4.5 간접 조명에 미치는 영향 (Common.hlsli)

`getAmbientColor()`는 Skybox와 동일한 상수(b9)를 사용하여 씬의 간접 조명 색상을 결정한다.

```hlsl
float3 getAmbientColor()
{
    float sunAniso = max(dot(lightDir, eyeDir), 0.0);
    float3 eyeHorizonColor = lerp(normalHorizonColor, sunHorizonColor, sunAniso);

    float3 ambientColor = float3(1.0, 1.0, 1.0);
    float sunAltitude = lightDir.y;
    float dayAltitude = PI / 12.0;
    float maxHorizonAltitude = -PI / 24.0;

    if (sunAltitude <= dayAltitude)
    {
        float w = smoothstep(maxHorizonAltitude, dayAltitude, sunAltitude);
        ambientColor = lerp(eyeHorizonColor, ambientColor, w);
    }

    return ambientColor;
}
```

이 함수가 하는 일:

1. **카메라가 바라보는 방향에 따라** Horizon 색상을 Normal/Sun으로 보간한다 (`sunAniso`). 태양을 바라보면 태양빛 색조가, 반대면 일반 하늘 색조가 간접 조명에 반영된다

2. **태양 고도에 따라** 간접 조명을 어둡게 한다. 태양이 `dayAltitude`(π/12 ≈ 15°) 이상이면 완전한 백색(1, 1, 1)이지만, 수평선(`-π/24`) 이하로 내려가면 Horizon 색으로 치환된다. `smoothstep`으로 부드럽게 전환된다

이 ambient 색상은 `getDiffuseTerm()`과 `getSpecularTerm()`에서 IBL 근사 조명으로 사용되므로, **하늘 색이 바뀌면 씬 전체의 조명 톤이 함께 바뀐다**. 석양 때 세상이 붉게 물들고, 밤에는 어두운 청색조를 띄는 것이 이 구조에서 자연스럽게 나온다.

### 4.6 전체 파이프라인 요약

```
[CPU — Skybox::Update(dateTime)]
시간(tick) → Normal/Sun × Horizon/Zenith 4색 결정 (Lerp)
     ↓ sRGB → Linear → SkyboxConstantBuffer (b9)

[GPU — SkyboxPS.hlsl]
posDir(방향벡터)
     ├── getPlanetTexcoord() → 태양/달 텍스처 렌더링
     └── 배경 하늘 색 결정
          ├── dot(lightDir, eyeDir) → sunDirWeight → Horizon/Zenith 색 선택
          └── sin(posDir.y) → 고도 → Horizon ↔ Zenith 보간 (pow 커브)

[GPU — Common.hlsli::getAmbientColor()]
동일한 b9 상수 → 카메라 방향 + 태양 고도 → 간접 조명 색
     ↓ getDiffuseTerm(), getSpecularTerm()에서 사용
     → 씬 전체의 조명 톤이 하늘과 일관
```

## 5. 문제점 & 해결

### 5.1 수평선 색이 하늘 전체로 번지는 문제

**문제**: 단순 선형 보간으로 Horizon-Zenith을 섞으면, 석양의 붉은 Horizon 색이 하늘 전체에 퍼져 비현실적으로 보인다.

**해결**: Mix Color(`(Horizon + Zenith) * 0.5`)를 경계로 두 구간을 나누고, 수평선 근처에는 `pow(x, 15.0)`의 급격한 커브를, 상공에는 `pow(x, 0.5)`의 완만한 커브를 적용했다. 이로써 Horizon 색은 수평선 얇은 띠에만 집중되고, 하늘 대부분은 부드러운 그라데이션을 유지한다.

### 5.2 일출/일몰 색의 독립적 전환

**문제**: Normal 색(일반 방향)과 Sun 색(태양 방향)이 같은 전환 구간을 사용하면, 태양 반대쪽 하늘도 동시에 붉어지는 부자연스러운 결과가 나온다.

**해결**: Normal 색은 Day ↔ Night 2단계 전환, Sun 색은 Day → Sunset → Night → Sunrise → Day 4단계 전환으로 분리했다. Sun 색은 추가로 `MAX_SUNSET`(12500)과 `MAX_SUNRISE`(23500) 피크 시점을 거치면서 일몰/일출 고유의 붉은색을 표현한다. PS에서 `sunDirWeight`에 의해 태양 방향에서만 이 색이 드러나므로, 반대쪽 하늘은 Normal 색을 유지한다.

### 5.3 View Transform에서의 Translation 제거

**문제**: 일반적인 View 변환을 적용하면 스카이박스가 카메라 위치에 따라 이동하여, 가까운 배경처럼 보인다.

**해결**: VS에서 `mul(float4(pos, 0.0), view)`로 w=0을 사용해 Translation 성분을 무시한다. 이후 `mul(float4(pos, 1.0), proj)`로 Projection만 적용하여, 스카이박스가 항상 무한히 먼 배경으로 보이게 한다.

## 6. 결과

- 큐브맵 에셋 없이 연속적인 낮/밤 전환과 일출/일몰 색상을 표현한다
- 태양 방향에서만 일출/일몰 색이 나타나고, 반대 방향은 일반 하늘색을 유지하여 자연스럽다
- 하늘 색상이 간접 조명에 자동 반영되어, 석양 때 씬 전체가 붉은 톤으로, 밤에는 어두운 남색 톤으로 일관성 있게 전환된다
- 달의 8단계 월상 변화와 태양/달의 방향 기반 크기 변화로 시각적 디테일을 더한다

## 7. 회고

- 현재 색상 프리셋이 코드에 하드코딩되어 있다. 데이터 파일이나 에디터에서 커브를 조정할 수 있으면 아티스트 워크플로우가 개선될 것이다
- `getAmbientColor()`가 Horizon 색상만 참고하고 Zenith는 사용하지 않는다. Zenith 색도 비율적으로 반영하면 천정이 보이는 상황(올려다볼 때)에서 더 정확한 간접 조명을 줄 수 있다
- Rayleigh/Mie 산란 같은 물리 기반 대기 모델을 적용하면 더 사실적인 하늘을 만들 수 있지만, 현재의 프리셋 보간 방식은 비용 대비 충분히 효과적이다
