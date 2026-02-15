# Lighting — 동적 태양광 기반 PBR 라이팅

## 1. 개요

Voxen의 라이팅 시스템은 **동적으로 회전하는 태양**을 광원으로, **Unreal Engine 스타일의 PBR(Physically Based Rendering)** 을 기반으로 한 셰이딩 파이프라인이다. G-Buffer에 기록된 Normal Map, MER(Metallic/Emission/Roughness) 텍스처 정보를 활용하여, Deferred Shading 단계에서 물리 기반의 Ambient + Direct Lighting을 계산한다.

핵심 구성 요소:

- **CPU (Light.cpp)** — 게임 시간에 따른 태양 방향, Radiance 색상/세기 계산 + Cascade Shadow Map 행렬 갱신
- **GPU (Common.hlsli)** — Schlick Fresnel, GGX NDF, Schlick-GGX Geometry 함수 기반 PBR BRDF
- **GPU (ShadingBasicPS.hlsl)** — G-Buffer 로드 후 Ambient + Direct Lighting 합산
- **GPU (BasicPS.hlsl)** — Normal Map, MER Atlas 를 G-Buffer에 기록

## 2. 도입 동기

복셀 렌더러에서 단순한 Lambertian Diffuse만 적용하면, 모든 블록이 균일한 재질감을 가져 시각적으로 단조롭다. 돌, 금속, 발광 블록 등 재질별 차이를 표현하려면 **Metallic/Roughness 기반의 PBR** 이 필요하다.

또한 태양이 고정된 방향이면 낮과 밤의 구분이 없어 몰입감이 떨어진다. **게임 시간에 따라 태양이 궤도를 회전**하고, 이에 맞춰 **Radiance 색상(Day → Sunset → Night → Sunrise)** 이 연속적으로 변해야 자연스러운 하루 주기를 표현할 수 있다.

이 두 가지 — 동적 태양광과 PBR — 을 결합하면, 일출 시 금속 블록에 붉은 스페큘러가 맺히거나, 밤에 발광 블록만 빛나는 등 **재질 × 시간대**에 따른 풍부한 시각적 변화가 가능해진다.

## 3. 핵심 아이디어

### 3.1 Unreal PBR 모델 차용

Epic Games의 ["Real Shading in Unreal Engine 4"](https://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf) 논문에서 제시한 Cook-Torrance Specular BRDF를 그대로 차용한다.

```
f_specular = (F · D · G) / (4 · NdotI · NdotO)
```

| 항목 | 함수 | 설명 |
|---|---|---|
| **F** (Fresnel) | Schlick Approximation | 시야각에 따른 반사율 변화 |
| **D** (Normal Distribution) | GGX/Trowbridge-Reitz | 마이크로면의 법선 분포 |
| **G** (Geometry) | Schlick-GGX | 마이크로면 자기 그림자(Self-Shadowing) |

Diffuse는 에너지 보존을 위해 `kd = (1 - F) × (1 - metallic)`로 가중하여, 금속일수록 Diffuse 기여가 줄고 Specular 기여가 지배적이 된다.

### 3.2 IBL 없는 환경에서의 Ambient/Specular 근사

Unreal의 원본 PBR은 **IBL(Image Based Lighting)** — Irradiance Map(Diffuse)과 Prefiltered Environment Map(Specular)을 사용한다. 그러나 이 프로젝트는 절차적 스카이박스를 사용하므로 큐브맵이 존재하지 않는다.

이를 대체하기 위해 **태양 방향과 스카이 색상**을 직접 활용한 근사를 설계했다:

- **Diffuse Irradiance 대체**: `(radianceColor × NdotL) + ambientColor`
  - Irradiance Map 대신 직접광(NdotL)과 환경광(ambientColor)을 합산
- **Specular Irradiance 대체**: `lerp(reflectRadiance, ambientColor, roughness)`
  - 반사 방향이 태양에 가까우면 `radianceColor`, 멀면 `ambientColor`를 사용
  - Roughness가 높을수록 환경광 비중 증가 (거친 표면 = 넓게 퍼진 반사)
- **Split-Sum Approximation**: 사전 계산된 **BRDF LUT** (`brdf.png`, `register(t10)`)를 활용
  - `specularBRDF = brdfTex.Sample(UV = (NdotV, 1 - roughness)).rg`
  - 최종 스페큘러: `specularIrradiance × (specularBRDF.r × F0 + specularBRDF.g)`

### 3.3 동적 태양과 Radiance 색상 전이

태양은 게임 시간(`dateTime`)에 비례하여 360° 궤도를 회전하며, 시간대별로 Radiance 색상이 부드럽게 전환된다:

```
DAY(1000~11000)  →  SUNSET(~12500)  →  NIGHT(13700~22300)  →  SUNRISE(~23500)  →  DAY
   White(1,1,1)     Orange(0.64,0.26,0.04)  Black(0,0,0)      Gold(0.72,0.60,0.34)
```

Radiance 세기(`radianceWeight`)는 태양 고도에 따른 **Smootherstep** 커브로 결정되어, 지평선 부근에서 급격히 감쇠한다.

## 4. 구현 내용

### 4.1 CPU: 태양 방향과 Radiance 계산 (Light.cpp)

#### 태양 궤도 회전

```cpp
float angle = (float)dateTime / Date::DAY_CYCLE_AMOUNT * 2.0f * PI;

Matrix rotationAxisMatrix = Matrix::CreateFromAxisAngle(
    Vector3(-cos(PI/4), 0.0f, cos(PI/4)), angle);

m_dir = Vector3::Transform(
    Vector3(cos(PI/4), 0.0f, cos(PI/4)), rotationAxisMatrix);
m_dir.Normalize();
```

태양 방향 벡터를 **45° 기울어진 축**(`(-cos45°, 0, cos45°)`)을 중심으로 회전시킨다. 이 축은 XZ 평면에서 대각선 방향이므로, 태양이 동쪽에서 떠서 서쪽으로 지는 것이 아니라 **대각선 궤도**를 따라 회전한다. `DAY_CYCLE_AMOUNT = 24000` 틱이 한 바퀴(2π)에 해당한다.

#### Radiance 세기 (radianceWeight)

```cpp
float currentAltitude = m_dir.y;
float nightEndAltitude = -1.7f / 12.0f * PI;

float w = (currentAltitude - nightEndAltitude) / (1.0f - nightEndAltitude);
m_radianceWeight = Smootherstep(0.0f, MAX_RADIANCE_WEIGHT, w);
```

태양의 Y 성분(고도)을 기준으로 세기를 결정한다:

- 밤 시간대(`NIGHT_START ~ NIGHT_END`)에는 **강제 0**
- 그 외 시간에는 고도를 `[nightEndAltitude, 1.0]` 범위에서 정규화하고 **Smootherstep** 으로 `[0, 2.0]`에 매핑
- Smootherstep은 `6t⁵ - 15t⁴ + 10t³`으로, 일반 smoothstep보다 경계에서 더 완만한 전환을 만든다
- `MAX_RADIANCE_WEIGHT = 2.0`으로, 한낮에 HDR 범위의 밝기를 허용한다

#### Radiance 색상 전이

```cpp
// Day → Sunset
m_radianceColor = Lerp(RADIANCE_DAY_COLOR, RADIANCE_SUNSET_COLOR, factor);
// Sunset → Night
m_radianceColor = Lerp(RADIANCE_SUNSET_COLOR, RADIANCE_NIGHT_COLOR, factor);
// Night → Sunrise
m_radianceColor = Lerp(RADIANCE_NIGHT_COLOR, RADIANCE_SUNRISE_COLOR, factor);
// Sunrise → Day
m_radianceColor = Lerp(RADIANCE_SUNRISE_COLOR, RADIANCE_DAY_COLOR, factor);
```

| 시간대 | 구간 | 색상 |
|---|---|---|
| Day | 1000 ~ 11000 | White (1.0, 1.0, 1.0) |
| Sunset Peak | 12500 | Orange (0.64, 0.26, 0.04) |
| Night | 13700 ~ 22300 | Black (0.0, 0.0, 0.0) |
| Sunrise Peak | 23500 | Gold (0.72, 0.60, 0.34) |

각 전환 구간에서 `factor`는 시간 진행률의 선형 보간이다. 최종 색상은 `SRGB2Linear()` 변환 후 GPU에 전달되어, 셰이더의 **선형 공간** 연산과 일관성을 유지한다.

### 4.2 G-Buffer 기록: Normal Map과 MER 텍스처 (BasicPS.hlsl)

Deferred Shading을 위해 G-Buffer Fill 단계에서 Per-Pixel Normal과 Material 정보를 기록한다.

#### Normal Mapping

```hlsl
float3 normalMapping(float2 texcoord, uint texIndex, float3 normal)
{
    float3 normalTex = normalAtlasTextureArray.Sample(pointWrapSS, float3(texcoord, texIndex)).rgb;
    normalTex = normalize(2.0 * normalTex - 1.0);  // [0,1] → [-1,1] (Tangent Space Normal)

    float3 N = normal;
    float3 T = getTangent(normal);
    float3 B = cross(N, T);

    float3x3 TBN = float3x3(T, B, N);

    return normalize(mul(normalTex, TBN));  // Tangent Space → World Space
}
```

복셀 렌더러에서 Normal Map 적용의 핵심은 **TBN 기저 구성**이다. 일반적인 메시 렌더러는 버텍스 데이터에 Tangent를 포함하지만, Greedy Meshing으로 생성된 복셀 면은 항상 축 정렬(Axis-Aligned)이므로 **면 법선으로부터 Tangent를 직접 결정**할 수 있다.

`getTangent()` 함수는 각 축 방향별로 고정된 Tangent를 반환한다:

| Face Normal | Tangent |
|---|---|
| +X (Right) | +Z |
| -X (Left) | -Z |
| +Y (Top) | +X |
| -Y (Bottom) | +X |
| +Z (Back) | -X |
| -Z (Front) | +X |

이는 복셀의 축 정렬 특성을 활용한 최적화로, Tangent를 버텍스에 저장할 필요가 없어 메모리를 절약한다.

Normal Map에서 읽은 Tangent Space Normal을 TBN 행렬과 곱하면 World Space Normal이 되어 G-Buffer(`normalEdge.xyz`)에 기록된다.

#### MER (Metallic / Emission / Roughness) 텍스처

```hlsl
output.mer = merAtlasTextureArray.Sample(pointWrapSS, float3(input.texcoord, input.texIndex));
```

블록 종류별로 3개의 Atlas 텍스처 배열을 사용한다:

| Atlas | Register | 역할 |
|---|---|---|
| `blockAtlasTextureArray` | t0 | Albedo (Base Color) |
| `normalAtlasTextureArray` | t1 | Normal Map (Tangent Space) |
| `merAtlasTextureArray` | t2 | MER — R: Metallic, G: Emission, B: Roughness |

MER은 하나의 텍스처에 3개의 재질 파라미터를 채널별로 압축한 구조다:

- **R (Metallic)**: 0.0 = 비금속(유전체), 1.0 = 금속. 금속은 Diffuse가 0이 되고 Albedo가 F0로 사용된다
- **G (Emission)**: 발광 정도. 라이팅과 무관하게 빛을 내는 블록(글로우스톤 등)에 사용
- **B (Roughness)**: 0.0 = 매끈한 거울면, 1.0 = 완전 거친 면. GGX NDF의 alpha 파라미터에 직결

`texIndex`로 블록 종류별 Atlas 슬라이스를 참조하므로, 블록마다 다른 재질 특성을 갖게 된다.

### 4.3 PBR Ambient Lighting (Common.hlsli)

Ambient Lighting은 **IBL(Image Based Lighting)의 근사**다. 환경 큐브맵이 없으므로, 스카이박스의 색상 정보를 활용하여 Diffuse와 Specular Irradiance를 근사한다.

#### Ambient Color 계산

```hlsl
float3 getAmbientColor()
{
    float sunAniso = max(dot(lightDir, eyeDir), 0.0);
    float3 eyeHorizonColor = lerp(normalHorizonColor, sunHorizonColor, sunAniso);

    float3 ambientColor = float3(1.0, 1.0, 1.0);
    float sunAltitude = lightDir.y;
    if (sunAltitude <= dayAltitude)
    {
        float w = smoothstep(maxHorizonAltitude, dayAltitude, sunAltitude);
        ambientColor = lerp(eyeHorizonColor, ambientColor, w);
    }

    return ambientColor;
}
```

- 한낮 고도(`dayAltitude = π/12`)보다 높으면 순백색 `(1,1,1)` — 충분히 밝은 환경광
- 고도가 낮아질수록 **Horizon Color**(스카이박스에서 가져온 지평선 색)로 전환
- `sunAniso = dot(lightDir, eyeDir)` — 카메라가 태양 쪽을 바라볼수록 `sunHorizonColor`(따뜻한 색) 비중 증가
- 이로써 일몰 시 태양 방향의 Ambient가 붉게 물드는 효과를 얻는다

#### Diffuse Term

```hlsl
float3 getDiffuseTerm(float3 albedo, float3 pixelToEye, float3 normal, float metallic)
{
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    float3 F = schlickFresnel(F0, max(0.0, dot(normal, pixelToEye)));
    float3 kd = lerp(1.0 - F, 0.0, metallic);

    float3 diffuseIrradiance = (radianceColor * max(dot(normal, lightDir), 0.0)) + getAmbientColor();

    return kd * albedo * diffuseIrradiance;
}
```

Unreal의 원본 코드에서 `diffuseIrradiance`는 Irradiance Cubemap에서 샘플링한 값이다. 큐브맵이 없으므로 이를 **직접광의 Lambertian 성분 + Ambient Color**로 대체한다. 물리적으로, 반구 전체에서 들어오는 확산광(Irradiance)을 "주광원 + 환경광"의 합으로 근사한 것이다.

`kd`는 에너지 보존 가중치로, Fresnel 반사가 많을수록 Diffuse 에너지가 줄어든다. 금속(`metallic = 1.0`)이면 `kd = 0`이 되어 Diffuse가 완전히 사라진다.

#### Specular Term

```hlsl
float3 getSpecularTerm(float3 albedo, float3 pixelToEye, float3 normal, float metallic, float roughness)
{
    float2 specularBRDF = brdfTex.Sample(pointClampSS, float2(dot(pixelToEye, normal), 1 - roughness)).rg;

    float3 reflectDir = normalize(reflect(-pixelToEye, normal));
    float reflectRadianceWeight = max(dot(reflectDir, lightDir), 0.0);
    float3 reflectRadiance = lerp(ambientColor, radianceColor, reflectRadianceWeight);
    float3 specularIrradiance = lerp(reflectRadiance, ambientColor, roughness);

    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    return specularIrradiance * (specularBRDF.r * F0 + specularBRDF.g);
}
```

Unreal의 원본에서 `specularIrradiance`는 Prefiltered Environment Map을 반사 방향으로 샘플링한 값이다. 이를 다음과 같이 근사한다:

1. **반사 방향과 태양 방향의 내적** → `reflectRadianceWeight`
   - 반사 방향이 태양을 가리키면 `radianceColor` (밝은 스페큘러)
   - 반대 방향이면 `ambientColor` (환경광 반사)
2. **Roughness에 따른 보간** → `lerp(reflectRadiance, ambientColor, roughness)`
   - Roughness가 낮으면(매끈하면) 반사 방향에 충실한 조명
   - Roughness가 높으면(거칠면) 방향 무관하게 환경광으로 수렴
   - 이는 거친 표면에서 반사가 넓게 퍼지는 물리적 현상을 단순하게 모델링한 것

3. **BRDF LUT (Split-Sum Approximation)**
   - `brdfTex`는 사전 계산된 2D 텍스처로, `(NdotV, roughness)`를 입력으로 `(scale, bias)` 값을 반환
   - 최종 결과: `specularIrradiance × (scale × F0 + bias)`
   - 이는 환경광 Specular의 Fresnel 항을 미리 적분(Pre-Integration)한 것으로, 실시간에서 적분 없이 정확한 에너지 분배를 가능하게 한다

### 4.4 PBR Direct Lighting (Common.hlsli)

```hlsl
float3 getDirectLighting(float3 normal, float3 position, float3 albedo,
                         float metallic, float roughness, bool useShadow)
{
    float3 pixelToEye = normalize(eyePos - position);
    float3 halfway = normalize(pixelToEye + lightDir);

    float NdotI = max(0.0, dot(normal, lightDir));
    float NdotH = max(0.0, dot(normal, halfway));
    float NdotO = max(0.0, dot(normal, pixelToEye));

    float3 F0 = lerp(0.04, albedo, metallic);
    float3 F = schlickFresnel(F0, max(0.0, dot(halfway, pixelToEye)));
    float  D = ndfGGX(NdotH, roughness);
    float3 G = schlickGGX(NdotI, NdotO, roughness);
    float3 specularBRDF = (F * D * G) / max(1e-5, 4.0 * NdotI * NdotO);

    float3 kd = lerp(float3(1,1,1) - F, float3(0,0,0), metallic);
    float3 diffuseBRDF = kd * albedo;

    float shadowFactor = useShadow ? getShadowFactor(position, normal) : 1.0;
    shadowFactor = pow(shadowFactor, 3.0);

    float3 radiance = radianceWeight * radianceColor * shadowFactor;

    return (diffuseBRDF + specularBRDF) * radiance * NdotI;
}
```

Direct Lighting은 태양 광원에 대한 **단일 방향광(Directional Light)** Cook-Torrance BRDF를 계산한다.

#### Schlick Fresnel — `schlickFresnel(F0, HdotV)`

```hlsl
float3 schlickFresnel(float3 F0, float NdotH)
{
    return F0 + (1 - F0) * pow(2, (-5.55473 * NdotH - 6.98316) * NdotH);
}
```

표준 Schlick 근사 `F0 + (1-F0)(1-cosθ)⁵` 대신 **Epic Games의 최적화 버전**을 사용한다. `pow(2, ...)` 형태로 변환하여 GPU의 `exp2` 명령어를 활용, `pow(x, 5)` 보다 빠르면서 오차가 매우 작다.

- `F0 = 0.04` (유전체 기본 반사율) ~ `albedo` (금속 반사율)
- `metallic`이 1이면 `F0 = albedo`가 되어 금속 특유의 컬러 반사를 표현

#### GGX NDF — `ndfGGX(NdotH, roughness)`

```hlsl
float ndfGGX(float NdotH, float roughness)
{
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
    return alpha2 / max(1e-5, PI * pow(NdotH * NdotH * (alpha2 - 1) + 1, 2));
}
```

마이크로면의 법선 분포를 결정한다. `roughness²`를 alpha로 사용하는 것은 Disney/Unreal의 관행으로, 아티스트가 조절하는 roughness 값의 **지각적 선형성(perceptual linearity)** 을 보장한다. 즉, roughness 0.5와 1.0 사이의 시각적 차이가 0.0과 0.5 사이의 차이와 비슷하게 느껴진다.

#### Schlick-GGX Geometry — `schlickGGX(NdotI, NdotO, roughness)`

```hlsl
float schlickGGX(float NdotI, float NdotO, float roughness)
{
    float k = (roughness + 1) * (roughness + 1) / 8;
    float gl = NdotI / (NdotI * (1 - k) + k);
    float gv = NdotO / (NdotO * (1 - k) + k);
    return gl * gv;
}
```

마이크로면이 서로를 가리는 **Self-Shadowing과 Self-Masking**을 모델링한다.

- `k = (roughness + 1)² / 8` — Direct Lighting용 k 값 (IBL은 `roughness² / 2` 사용)
- `gl × gv` — 빛 방향(I)과 시야 방향(O) 각각에 대한 Geometry Attenuation을 곱함
- Roughness가 높을수록 k가 커져 Geometry 감쇠가 강해지고, 결과적으로 Specular 하이라이트가 줄어든다

#### Shadow Factor와 최종 합산

```hlsl
float shadowFactor = useShadow ? getShadowFactor(position, normal) : 1.0;
shadowFactor = pow(shadowFactor, 3.0);

float3 radiance = radianceWeight * radianceColor * shadowFactor;

return (diffuseBRDF + specularBRDF) * radiance * NdotI;
```

- `shadowFactor`에 `pow(3.0)`을 적용하여 그림자 경계를 더 선명하게 만든다 (선형보다 급격한 감쇠)
- `radiance = weight × color × shadow` — 태양의 세기, 색상, 그림자를 모두 반영한 입사 에너지
- `NdotI` — Lambert Cosine 항. 빛이 비스듬히 들어올수록 에너지 밀도가 줄어드는 것을 반영

Shadow에서 주목할 부분은 밤 시간 처리다:

```hlsl
return shadowValue + (1.0 - shadowValue) * (1.0 - saturate(radianceWeight / maxRadianceWeight));
```

`radianceWeight`가 0에 가까운 밤에는 `shadowFactor`가 항상 1.0에 수렴한다. 빛이 없는 밤에 그림자를 그리는 것은 물리적으로 무의미하므로, 이를 자연스럽게 무효화한다.

### 4.5 Deferred Shading 합산 (ShadingBasicPS.hlsl)

```hlsl
float4 main(psInput input) : SV_TARGET
{
    // G-Buffer에서 4개 샘플 평균
    float3 normal   = avg(normalEdgeTex, 4 samples);
    float3 position = avg(positionTex, 4 samples);
    float3 albedo   = avg(albedoTex, 4 samples);
    float3 mer      = avg(merTex, 4 samples);

    float ao = pow(ssaoTex.Sample(...).r, 4.0);

    float3 ambientLighting = getAmbientLighting(ao, albedo, position, normal, metallic, roughness);
    float3 directLighting  = getDirectLighting(normal, position, albedo, metallic, roughness, true);

    return float4(clamp(ambientLighting + directLighting, 0.0, 1000.0), 1.0);
}
```

최종 조명은 `Ambient + Direct`의 단순 합산이다.

- **Ambient**: AO가 적용된 환경광 기반 PBR (Diffuse + Specular IBL 근사)
- **Direct**: 태양 방향광에 대한 Cook-Torrance BRDF + Cascade Shadow
- `ao = pow(ao, 4.0)` — SSAO 결과를 강화하여 차폐 효과를 더 극적으로 만든다
- 출력 클램프를 `1000.0`으로 설정하여 **HDR 범위를 유지**한 채 후처리(Bloom/Tone Mapping)로 넘긴다

Non-Edge 픽셀(`main`)은 4개 MSAA 샘플을 평균하여 1회만 라이팅을 계산하고, Edge 픽셀(`mainMSAA`)은 각 샘플별로 개별 라이팅을 수행한 뒤 평균한다.

### 4.6 전체 파이프라인 요약

```
[CPU - Light.cpp::Update()]
dateTime → 태양 방향(m_dir), Radiance 색상/세기 → LightConstantBuffer
         → Cascade Shadow ViewProj 행렬 → ShadowConstantBuffer

[GPU - BasicPS.hlsl (G-Buffer Fill)]
blockAtlasTextureArray → Albedo
normalAtlasTextureArray → TBN → World Space Normal
merAtlasTextureArray → Metallic / Emission / Roughness
        ↓
G-Buffer: NormalEdge, Position, Albedo, Coverage, MER

[GPU - ShadingBasicPS.hlsl (Deferred Shading)]
G-Buffer Load + SSAO
        ↓
getAmbientLighting()
  ├─ getDiffuseTerm: kd × albedo × (NdotL × radianceColor + ambientColor)
  └─ getSpecularTerm: specIrradiance × (brdfLUT.r × F0 + brdfLUT.g)
        +
getDirectLighting()
  ├─ Diffuse: kd × albedo
  ├─ Specular: (F × D × G) / (4 × NdotI × NdotO)
  └─ × radianceWeight × radianceColor × shadowFactor × NdotI
        ↓
HDR Output → Post-Processing (Bloom + Tone Mapping)
```

## 5. 문제점 & 해결

### 5.1 IBL 없이 Specular 환경 반사 표현

**문제**: Prefiltered Environment Map 없이 금속 표면의 환경 반사를 어떻게 근사할 것인가.

**해결**: 반사 방향과 태양 방향의 내적을 기반으로 `lerp(ambientColor, radianceColor, reflectWeight)`를 구성하고, roughness로 한 번 더 보간하여 `lerp(reflectRadiance, ambientColor, roughness)`로 최종 스페큘러 환경광을 결정했다. 큐브맵의 정밀한 반사 대신 **광원 방향 1개 + 환경색**만으로 반사를 근사한 것이다.

트레이드오프로, 금속 블록에 주변 블록이 반사되는 효과는 표현할 수 없다. 그러나 복셀 아트 스타일에서 이 수준의 정밀한 반사는 크게 요구되지 않으며, 태양 하이라이트와 환경색 변화만으로 충분한 금속감을 표현할 수 있다.

### 5.2 밤 시간의 Shadow 아티팩트

**문제**: `radianceWeight`가 0인 밤에도 그림자 계산이 수행되면, PCF 편향이나 부정확한 그림자 맵이 비정상적인 어둡기/밝기를 만들 수 있다.

**해결**: `getShadowFactor()` 반환값에서 `radianceWeight / maxRadianceWeight`를 이용해 밤에 shadowFactor를 1.0으로 수렴시킨다. 빛이 없는 상황에서 그림자를 자연스럽게 무효화하여 아티팩트를 방지한다.

### 5.3 sRGB ↔ Linear 공간 관리

**문제**: CPU에서 정의한 Radiance 색상이 sRGB 공간이지만, 셰이더의 PBR 연산은 Linear 공간을 전제한다.

**해결**: CPU에서 `SRGB2Linear()` 변환 후 GPU에 전달한다. 셰이더 내부의 모든 색상 연산은 Linear 공간에서 수행되며, 최종 출력 후 Tone Mapping 단계에서 디스플레이에 맞는 감마 보정을 적용한다.

## 6. 결과

- 한낮에 금속 블록은 선명한 스페큘러 하이라이트를 보이고, 돌/흙 블록은 매트한 Diffuse 반사를 보여 **재질별 차이가 명확하게 표현**된다
- 일출/일몰 시 Radiance 색상이 Specular에 반영되어, 금속 표면에 **주황/금색 하이라이트**가 맺힌다
- 밤에는 Radiance가 0으로 수렴하여 Ambient만 남고, 발광 블록(Emission)만 빛을 유지한다
- BRDF LUT를 활용한 Split-Sum Approximation으로, 큐브맵 없이도 **roughness에 따른 Specular 변화**가 자연스럽다
- Normal Map 적용으로 평면적인 복셀 면에도 **미세한 요철과 방향성**이 생겨 디테일이 향상된다

## 7. 회고

- IBL 근사를 태양 방향 1개로 단순화했기 때문에, **다중 광원**(횃불, 용암 등)이 추가되면 현재 구조로는 환경 반사를 정확히 표현하기 어렵다. 향후 다중 광원을 지원하려면 포인트 라이트 클러스터링이나 간단한 프로브 기반 GI를 고려할 수 있다
- Emission 채널이 MER에 존재하지만, 현재 Deferred Shading 단계에서 Emission 기반 Bloom이나 자체 발광 처리가 별도로 분리되어 있지 않다. Emission 값을 HDR 출력에 더하는 후처리 패스를 추가하면 발광 효과를 더 극적으로 표현할 수 있을 것이다
- `pow(ao, 4.0)`과 `pow(shadowFactor, 3.0)` 같은 하드코딩된 지수값들은 시각적 튜닝의 결과이나, 파라미터화하여 에디터에서 실시간 조절할 수 있으면 이터레이션이 빨라질 것이다
- 현재 BRDF LUT는 외부 이미지(`brdf.png`)를 사용한다. 셰이더에서 동적으로 생성하면 파일 의존성을 줄일 수 있지만, 사전 계산의 장점(로드 시 한 번만 읽으면 됨)을 고려하면 현재 방식도 합리적이다
