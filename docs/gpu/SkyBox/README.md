# Dynamic Skybox — 셰이더 기반 절차적 하늘 렌더링

<img width="800" height="459" alt="Image" src="https://github.com/user-attachments/assets/766a6919-d729-4296-9f77-575a359d18d5" />
<img width="800" height="459" alt="Image" src="https://github.com/user-attachments/assets/6703c1dd-1501-4ca8-ac60-5788c59d849f" />
<img width="800" height="459" alt="Image" src="https://github.com/user-attachments/assets/343f50d8-43be-4317-b774-802be186387f" />

## 1. 개요

큐브맵 텍스처 없이, **셰이더만으로** 동적 하늘을 렌더링하는 시스템이다.
CPU에서 게임 내 시간에 따라 하늘 색상 4종을 결정하여 Constant Buffer로 전달하고 Pixel Shader가 픽셀의 방향(고도, 태양 방향과의 각도)에 따라 이 색상들을 혼합하여 최종 하늘색을 만든다.
이 하늘 색상은 Skybox 렌더링뿐 아니라 씬 전체의 간접 조명(Ambient Light) 색상에도 동일하게 반영된다.

## 2. 도입 동기

마인크래프트에서 자연스러운 배경 변화가 인상 깊었고 그로인해 동적 큐브맵을 쉐이더로 구현하기로 결정
낮/밤 전환, 일출/일몰의 색 변화를 표현

## 3. 핵심 아이디어

하늘의 색은 두 가지 축으로 결정된다:

1. **고도(Altitude)**

- 수평선(Horizon)에서 천정(Zenith)으로 갈수록 색이 변한다.
  - Horizon의 범위: [-1, PI/24.0]
  - Zenith의 범위: [PI/24.0, 1]

2. **태양 방향(Sun Direction)**

- 태양을 바라보는 방향에서는 태양 색이 강해지고, 반대 방향에서는 일반 하늘 색이 된다

3. **색 혼합 방식**

- 태양을 바라보지 않는 normal[Zenith|Horizon]색과 태양을 바라보는 쪽인 Sun[Zenith|Horizon]을 포함한 4개의 색상을 가지고 태양을 바라보는 방향에 따라 적절히 보간합니다.
- CPU에서는 이 4가지의 색을 시간(DateTime)에 따라 색상 프리셋을 적절히 보간하여 결정하고 GPU에 전달합니다.
- GPU에서는 이 4가지색을 이용하여 태양에 보는 방향에 따른 Zenith, Horizon을 결정하고, Skybox의 방향벡터를 기준으로 Zenith, Horizon을 보간합니다.

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

| 구간            | Tick 범위            | 설명           |
| --------------- | -------------------- | -------------- |
| Day             | 1000 ~ 11000         | 낮 (색 고정)   |
| Day → Sunset    | 11000 ~ 12500        | 낮->석양 보간  |
| Sunset → Night  | 12500 ~ 13700        | 석양->밤 보간  |
| Night           | 13700 ~ 22300        | 밤 (색상 고정) |
| Night → Sunrise | 22300 ~ 23500        | 밤->일출 보간  |
| Sunrise → Day   | 23500 ~ 25000(=1000) | 일출->낮 보간  |

### 4.2 CPU: 시간 기반 색상 결정 (Skybox.cpp)

`Update(dateTime)`에서 현재 tick에 따라 4개의 색상 벡터를 결정한다.

#### 색상 프리셋 (Skybox.h, sRGB)

| 색상             | Horizon                          | Zenith                                          |
| ---------------- | -------------------------------- | ----------------------------------------------- |
| **Normal Day**   | (0.67, 0.82, 1.0) 밝은 하늘색    | (0.52, 0.67, 1.0) 진한 파랑                     |
| **Normal Night** | (0.19, 0.20, 0.24) 어두운 회청색 | (0.17, 0.175, 0.195) 더 어두운 남색             |
| **Sun Day**      | (0.60, 0.74, 1.0) 연한 파랑      | (0.32, 0.45, 1.0) 깊은 파랑                     |
| **Sun Sunrise**  | (0.72, 0.60, 0.34) 따뜻한 주황   | SunDay↔Night 보간(일몰-일출은 수평색이 더 중요) |
| **Sun Sunset**   | (0.64, 0.26, 0.04) 붉은 주황     | SunDay↔Night 보간(일몰-일출은 수평색이 더 중요) |

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

고정 구간(Day, Night)가 아닌 경우 Normal, Sun 서로 다른 보간 구간을 가집니다.

- **Normal 색**: Day ↔ Night 사이를 단순하게 보간
- **Sun 색**: Day → Sunset → Night → Sunrise → Day로 4단계 전환, 일출/일몰 색을 별도로 거침

최종 4색은 `sRGB → Linear` 변환 후 `SkyboxConstantBuffer (b9)`로 GPU에 전달한다.

### 4.3 Vertex Shader: 방향 벡터 전달 (SkyboxVS.hlsl)

VS에서 핵심은 **카메라 위치(포인트)를 무시하고 방향만 전달**

```hlsl
output.posProj = mul(float4(output.posProj.xyz, 0.0), view);  // w=0 → 이동 제거
output.posProj = mul(float4(output.posProj.xyz, 1.0), proj);  // w=1 → 투영 적용
```

View 변환 시 `w=0`으로 곱하면 Translation이 무시되어, 카메라가 어디에 있든 스카이박스가 항상 카메라 중심에 위치하게 된다.
`posWorld`를 PS에서 `Normalize()`로 방향 벡터로 사용된다.

### 4.4 Pixel Shader: 하늘 색상 결정 (SkyboxPS.hlsl)

#### 4.4.1 태양/달 렌더링과 getPlanetTexcoord TBN 변환

태양과 달은 따로 메쉬를 구성 후 텍스쳐를 입히지 않고 Skybox의 posDir 이용해 텍스쳐를 투영합니다.
이 함수의 핵심은 **3D 구면 방향을 천체 표면의 2D 텍스처 좌표로 변환**하는 것이며, 이를 위해 TBN 좌표계 변환을 활용합니다.

```hlsl
bool getPlanetTexcoord(float3 posDir, float3 planetDir, float size, out float2 outTexcoord)
{
    outTexcoord = float2(0.0, 0.0);
    bool ret = false;

    float PDotP = dot(planetDir, posDir);
    float3 v = normalize(posDir - PDotP * planetDir);
    float3 p = v * tan(acos(PDotP));

    if (PDotP > 0.0 && length(p) < size)
    {
        float3 N = -planetDir;
        float3 T = float3(cos(PI / 4.0), 0.0, -cos(PI / 4.0));
        float3 B = cross(N, T);
        float3x3 TBNMatrix = float3x3(T, B, N);

        float3 vTBN = mul(p, transpose(TBNMatrix)); // 직교 행렬의 역행렬은 전치행렬

        outTexcoord.x = (vTBN.x / size + 1.0) * 0.5; // vTBN.x => [-size, size]
        outTexcoord.y = (vTBN.y / size + 1.0) * 0.5; // vTBN.x => [-size, size]
        ret = true;
    }

    return ret;
}
```

이 변환은 3단계로 진행된다:

1. 3차원 방향 벡터를 천체의 방향에 맞춰 내적 후 크기 결정

![Image](https://github.com/user-attachments/assets/9c4b9cbd-47b6-49ef-8b81-713543a1a577)

2. 천체 내외부 판단 후 TBN 좌표로 변환

- 단순히 각도와 투영된 벡터의 길이를 기준으로 내외부 판단
- 현재 투영된 좌표(`p`)는 월드 좌표
  - 이를 TBN 좌표계 내부의 좌표로 변환하여 텍스쳐 좌표로 활용해야 함
- 월드좌표계 기준으로 만들어진 `TBN Matrix * TBN좌표계의 위치 => 월드좌표`
  - 여기서 원하는건 `월드좌표`를 원하는게 아닌 `TBN좌표계의 위치`를 원함
  - 월드좌표계 기준으로 만들어진 `TBN Matrix`의 역행렬을 월드좌표에 곱해서 얻을 것
  - 직교 행렬의 역행렬은 전치행렬임

```
if (PDotP > 0.0 && length(p) < size)
    {
        float3 N = -planetDir;
        float3 T = float3(cos(PI / 4.0), 0.0, -cos(PI / 4.0));
        float3 B = cross(N, T);
        float3x3 TBNMatrix = float3x3(T, B, N);

        float3 vTBN = mul(p, transpose(TBNMatrix));
        ...
    }
```

3. TBN 좌표를 적절히 텍스쳐 좌표로 변환

- `vTBN.xy`의 범위가 `[-size, size]`이기에 텍스쳐 좌표`[0, 1]`에 맞게 적절히 스케일링
- 여기서 y값은 TBN에서 B가 아랫방향이라 부호를 뒤집지 않아도 옳은 방향임
- cf. 2D 원에 대해서 텍스쳐링임 -> 사각형 아님에 명심

```
outTexcoord.x = (vTBN.x / size + 1.0) * 0.5; // vTBN.x => [-size, size]
outTexcoord.y = (vTBN.y / size + 1.0) * 0.5; // vTBN.x => [-size, size]
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
float posAltitude = posDir.y;
float3 mixColor = (horizonColor + zenithColor) * 0.5;
float horizonAltitude = sin(PI / 24.0);

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
    float dayAltitude = sin(PI / 24.0);
    float maxHorizonColorAltitude = -sin(PI / 24.0);
    if (sunAltitude <= dayAltitude)
    {
        float w = smoothstep(maxHorizonColorAltitude, dayAltitude, sunAltitude);
        ambientColor = lerp(eyeHorizonColor, ambientColor, w);
    }

    return ambientColor;
}
```

이 함수가 하는 일:

1. **카메라가 바라보는 방향에 따라** Horizon 색상을 Normal/Sun으로 보간한다 (`sunAniso`). 태양을 바라보면 태양빛 색조가, 반대면 일반 하늘 색조가 간접 조명에 반영된다

2. **태양 고도에 따라** 간접 조명을 어둡게 한다. 태양이 `dayAltitude`(π/12 ≈ 15°) 이상이면 완전한 백색(1, 1, 1)이지만, 수평선(`-π/24`) 이하로 내려가면 Horizon 색으로 치환된다. `smoothstep`으로 부드럽게 전환된다

이 ambient 색상은 `getDiffuseTerm()`과 `getSpecularTerm()`에서 IBL 근사 조명으로 사용되므로, **하늘 색이 바뀌면 씬 전체의 조명 톤이 함께 바뀐다**. 석양 때 세상이 붉게 물들고, 밤에는 어두운 청색조를 띄는 것이 이 구조에서 자연스럽게 나온다.

## 5. 회고

- 단순히 정적 큐브맵을 사용하기보다 더 효과적인 배경을 구현할 수 있었음
- Minecraft와 매우 유사한 하늘 구현에 성공해서 좋았음
- 시간에 따른 하늘색과 보간의 방식의 경우의 수, 간접조명엔 어떤 영향을 줄건지, `posDir`만을 가지고 텍스쳐를 어떻게 만들지에 관련된 고민이 많았지만 해결했음
- 사실적인 하늘 묘사(산란 및 대기)과는 거리가 먼 쉐이더 작성이였음
