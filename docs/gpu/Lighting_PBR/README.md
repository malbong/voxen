# Lighting — 동적 태양광 기반 PBR 라이팅

## 1. 개요

Voxen의 라이팅 시스템은 **동적으로 회전하는 태양**을 단일 광원으로, **Unreal Engine 스타일의 PBR(Physically Based Rendering)** 을 기반으로 한 셰이딩 파이프라인이다.

G-Buffer에 기록된 Normal Map, MER(Metallic/Emission/Roughness) 텍스처 정보를 Deferred Shading 단계에서 읽어 물리 기반의 Ambient + Direct Lighting을 계산한다.

핵심 구성 요소:

- **CPU (Light.cpp)** — 게임 시간에 따른 태양 방향, Radiance 색상/세기 계산 + Cascade Shadow Map 행렬 갱신
- **GPU (Lighting.hlsli)** — Schlick Fresnel, GGX NDF, Schlick-GGX Geometry, IBL 근사, Shadow 함수
- **GPU (ShadingBasicPS.hlsl)** — G-Buffer 로드 후 Ambient + Direct Lighting 합산
- **GPU (BasicPS.hlsl)** — Normal Map, MER Atlas를 G-Buffer에 기록

## 2. 도입 동기

복셀 렌더러에서 단순한 Lambertian Diffuse만 적용하면, 모든 블록이 균일한 재질감을 가져 시각적으로 단조롭다. 돌, 금속, 발광 블록 등 재질별 차이를 표현하려면 **Metallic/Roughness 기반의 PBR** 이 필요하다.

또한 태양이 고정된 방향이면 낮과 밤의 구분이 없어 몰입감이 떨어진다. **게임 시간에 따라 태양이 궤도를 회전**하고, 이에 맞춰 **Radiance 색상(Day → Sunset → Night → Sunrise)** 이 연속적으로 변해야 자연스러운 하루 주기를 표현할 수 있다.

이 두 가지 — 동적 태양광과 PBR — 를 결합하면, 일출 시 금속 블록에 붉은 스페큘러가 맺히거나, 밤에 발광 블록만 빛나는 등 **재질 × 시간대**에 따른 풍부한 시각적 변화가 가능해진다.

## 3. 핵심 아이디어

### 3.1 Unreal PBR 모델 차용

Epic Games의 ["Real Shading in Unreal Engine 4"](https://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf) 논문에서 제시한 Cook-Torrance Specular BRDF를 기반으로 한다.

```
f_specular = (F · D · G) / (4 · NdotI · NdotO)
```

| 항목 | 함수 | 설명 |
|---|---|---|
| **F** (Fresnel) | Schlick Approximation | 시야각에 따른 반사율 변화 |
| **D** (Normal Distribution) | GGX/Trowbridge-Reitz | 마이크로면의 법선 분포 |
| **G** (Geometry) | Schlick-GGX | 마이크로면 자기 그림자(Self-Shadowing) |

Diffuse는 에너지 보존을 위해 `kd = (1 - F) × (1 - metallic)`로 가중하여, 금속일수록 Diffuse 기여가 줄고 Specular 기여가 지배적이 된다.

### 3.2 IBL 없는 환경에서의 Ambient 근사

Unreal의 원본 PBR은 **IBL(Image Based Lighting)** 을 사용한다.

| IBL 구성 요소 | Unreal 원본 | Voxen 근사 |
|---|---|---|
| **Diffuse Irradiance** | Irradiance Cubemap 샘플링 | `(radianceColor × NdotL) + ambientColor` |
| **Specular Irradiance** | Prefiltered Environment Map (roughness→mip) | `getSkyColor(reflectDir)` (절차적 하늘) |
| **Split-Sum** | Pre-integrated BRDF LUT | 동일하게 `brdfTex` 사용 |

이 프로젝트는 절차적 스카이박스를 사용하므로 큐브맵이 존재하지 않는다. 대신 **절차적으로 계산되는 하늘 색상 함수**를 IBL 대체제로 활용한다.

```
IBL Specular 근사:
  낮은 roughness → getSkyColor(reflectDir)로 하늘을 직접 반사 (거울 느낌)
  높은 roughness → diffuseIrradiance로 수렴 (Lambertian에 가까운 확산)

  lerp(reflectionColor × reflectRadianceWeight, diffuseIrradiance, roughness)
```

#### getSkyColor: 절차적 하늘의 IBL 대체

```hlsl
float3 getSkyColor(float3 posDir)
{
    // 태양/달 텍스처
    if (...getPlanetTexcoord(posDir, lightDir, sunSize, sunTexcoord))
        skyColor += sunTex.SampleLevel(...) * radianceWeight;
    if (...getPlanetTexcoord(posDir, -lightDir, moonSize, moonTexcoord))
        skyColor += moonTex.SampleLevel(...) * moonRadianceWeight;

    // Horizon ~ Zenith 그라디언트
    if (posAltitude <= horizonAltitude)
        skyColor += lerp(horizonColor, mixColor, pow(..., 15.0));
    else
        skyColor += lerp(mixColor, zenithColor, pow(..., 0.5));

    return skyColor;
}
```

반사 방향(`reflectDir`)을 이 함수에 넣으면, 해당 방향에서 보이는 하늘 색상을 반환한다. Prefiltered Environment Map에서 반사 방향으로 샘플링하는 것과 동일한 역할을 수식으로 대체한 것이다.

`useSkyColor` 플래그로 `getSkyColor`와 단순 `ambientColor` 중 선택할 수 있다. 수면 반사(`mainMirror`)처럼 sky 텍스처가 바인딩되지 않는 패스에서는 `useSkyColor = false`로 호출한다.

### 3.3 GGX NDF와 Roughness Bias

GGX NDF는 `roughness → 0`일 때 Dirac delta 함수에 수렴하여 픽셀 대부분에서 D ≈ 0을 반환한다. Direct Lighting의 날카로운 스페큘러는 반사 방향이 정확히 맞는 극소수 픽셀에만 나타난다.

완전한 PBR 파이프라인에서는 이 날카로운 Direct Specular를 **IBL Specular(환경 반사)** 가 보완한다. 낮은 roughness 표면은 낮은 mip의 Environment Map을 샘플링하여 주변 환경이 선명하게 반사되는 "거울 느낌"을 표현한다.

이 프로젝트는 IBL Specular를 `getSkyColor`로 근사하므로, 광원이 하늘뿐인 단순한 반사만 가능하다. 따라서 낮은 roughness에서 Direct Specular spike가 두드러지는 현상을 방지하기 위해 최소 roughness를 올리는 bias를 적용한다.

```hlsl
// ShadingBasicPS.hlsl
float roughnessBias = 0.05;
float roughness = min(1.0, mer.b + roughnessBias);
```

이는 NDF를 "고치는" 것이 아니라, **없는 IBL Specular의 에너지를 roughness를 높여 Direct Specular를 억제함으로써 보상**하는 것이다. Unreal Engine도 내부적으로 `roughness = max(roughness, 0.045)`와 같은 처리를 한다.

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

태양 방향 벡터를 **45° 기울어진 축**(`(-cos45°, 0, cos45°)`)을 중심으로 회전시킨다. `DAY_CYCLE_AMOUNT = 24000` 틱이 한 바퀴(2π)에 해당한다.

#### Radiance 세기와 색상 전이

```cpp
// 태양 고도 기반 Smootherstep으로 세기 결정
float w = (currentAltitude - nightEndAltitude) / (1.0f - nightEndAltitude);
m_radianceWeight = Smootherstep(0.0f, MAX_RADIANCE_WEIGHT, w);
```

```
DAY(1000~11000)  →  SUNSET(~12500)  →  NIGHT(13700~22300)  →  SUNRISE(~23500)  →  DAY
   White(1,1,1)     Orange(0.64,0.26,0.04)  Black(0,0,0)      Gold(0.72,0.60,0.34)
```

`MAX_RADIANCE_WEIGHT = 2.0`으로, 한낮에 HDR 범위의 밝기를 허용한다. Smootherstep(`6t⁵ - 15t⁴ + 10t³`)은 지평선 부근에서 급격하지 않은 완만한 전환을 만든다. 최종 색상은 `sRGB → Linear` 변환 후 GPU에 전달한다.

### 4.2 G-Buffer 기록: Normal Map과 MER 텍스처 (BasicPS.hlsl)

#### Normal Mapping

```hlsl
float3 normalMapping(float2 texcoord, uint texIndex, float3 normal)
{
    float3 normalTex = normalAtlasTextureArray.Sample(...).rgb;
    normalTex = normalize(2.0 * normalTex - 1.0); // [0,1] → [-1,1] Tangent Space

    float3 N = normal;
    float3 T = getTangent(normal); // 면 법선으로부터 직접 결정
    float3 B = cross(N, T);
    float3x3 TBN = float3x3(T, B, N);

    return normalize(mul(normalTex, TBN)); // Tangent → World Space
}
```

Greedy Meshing으로 생성된 복셀 면은 항상 축 정렬(Axis-Aligned)이므로, 버텍스 데이터 없이 **면 법선으로부터 Tangent를 직접 결정**할 수 있다.

| Face Normal | Tangent |
|---|---|
| +X, -X | ±Z |
| +Y, -Y | +X |
| +Z, -Z | ∓X |

#### MER (Metallic / Emission / Roughness) 텍스처

| Atlas | Register | 역할 |
|---|---|---|
| `blockAtlasTextureArray` | t0 | Albedo |
| `normalAtlasTextureArray` | t1 | Normal Map (Tangent Space) |
| `merAtlasTextureArray` | t2 | R: Metallic, G: Emission, B: Roughness |

### 4.3 Ambient Lighting (Lighting.hlsli)

#### Ambient Color

```hlsl
float3 getAmbientColor()
{
    float3 ambientColor = float3(1.0, 1.0, 1.0); // 한낮 = 순백색
    if (sunAltitude <= dayAltitude)
    {
        float sunAniso = max(dot(lightDir, eyeDir), 0.0);
        float3 eyeHorizonColor = lerp(normalHorizonColor, sunHorizonColor, sunAniso);
        float w = smoothstep(maxHorizonColorAltitude, dayAltitude, clamp(sunAltitude, ...));
        ambientColor = lerp(eyeHorizonColor, ambientColor, w);
    }
    return ambientColor;
}
```

태양 고도가 낮아질수록 Horizon Color로 전환한다. 카메라가 태양 방향을 바라볼수록(`sunAniso`) 따뜻한 `sunHorizonColor` 비중이 증가해, 일몰 시 태양 방향의 Ambient가 붉게 물든다.

#### Diffuse Term

```hlsl
float3 getDiffuseTerm(float3 albedo, float3 pixelToEye, float3 normal, float metallic)
{
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    float3 F  = schlickFresnel(F0, max(0.0, dot(normal, pixelToEye)));
    float3 kd = lerp(1.0 - F, 0.0, metallic);

    // Unreal 원본: irradianceIBLTex.Sample(linearSampler, normalWorld)
    // 근사:        직접광 Lambertian + 환경광
    float3 diffuseIrradiance = (radianceColor * max(dot(normal, lightDir), 0.0)) + getAmbientColor();

    return kd * albedo * diffuseIrradiance;
}
```

Irradiance Cubemap 대신 **"주광원의 Lambertian + 환경광"의 합**으로 근사한다. 물리적으로 반구 전체에서 들어오는 확산광(Irradiance)을 단순화한 것이다.

`kd`는 에너지 보존 가중치로, Fresnel 반사가 많을수록 Diffuse 에너지가 줄어든다. `metallic = 1.0`이면 `kd = 0`이 되어 Diffuse가 완전히 사라진다.

#### Specular Term

```hlsl
float3 getSpecularTerm(..., bool useSkyColor)
{
    float3 ambientColor     = getAmbientColor();
    float3 diffuseIrradiance = (radianceColor * max(dot(normal, lightDir), 0.0)) + ambientColor;

    float3 reflectDir       = normalize(reflect(-pixelToEye, normal));
    float3 reflectionColor  = useSkyColor ? getSkyColor(reflectDir) : ambientColor;
    float  reflectRadianceWeight = abs(dot(reflectDir, lightDir));
    float3 reflectRadiance  = reflectionColor * reflectRadianceWeight;

    // Unreal 원본: specularIBLTex.SampleLevel(linearSampler, reflectDir, roughness * 5.0).rgb
    // 근사:        getSkyColor(reflectDir) → roughness에 따라 diffuseIrradiance로 보간
    float3 specularIrradiance = lerp(reflectRadiance, diffuseIrradiance, roughness);

    float2 specularBRDF = brdfTex.Sample(pointClampSS, float2(dot(pixelToEye, normal), 1 - roughness)).rg;
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);

    return specularIrradiance * (specularBRDF.r * F0 + specularBRDF.g);
}
```

Specular Irradiance 근사 과정:

1. **`getSkyColor(reflectDir)`** — 반사 방향에서 보이는 절차적 하늘 색상 → Prefiltered Environment Map 대체
2. **`abs(dot(reflectDir, lightDir))`** — 반사 방향이 태양 또는 달을 향할수록 가중치가 높아진다. `max` 대신 `abs`를 사용하는 이유는 달 때문이다. 밤에 달은 `-lightDir` 방향에 위치하므로 `dot(reflectDir, lightDir)`이 음수가 되는데, `max`로 클리핑하면 달 방향 반사의 가중치가 0이 되어 `getSkyColor`가 반환한 달빛이 사라진다. `abs`는 태양/달 양쪽 방향 모두에서 대칭적으로 가중치를 부여한다.
3. **`lerp(..., diffuseIrradiance, roughness)`** — roughness가 높을수록 방향성 없는 확산광으로 수렴. 거친 표면은 반사가 넓게 퍼지는 물리적 현상을 단순화한 것

**Split-Sum Approximation**: `brdfTex`는 사전 계산된 2D LUT로, `(NdotV, 1-roughness)` 입력에서 `(scale, bias)`를 반환한다. 최종 결과: `specularIrradiance × (scale × F0 + bias)`. 실시간 적분 없이 Fresnel의 에너지 분배를 정확하게 반영한다.

### 4.4 Direct Lighting (Lighting.hlsli)

```hlsl
float3 getDirectLighting(float3 normal, float3 position, float3 albedo,
                         float metallic, float roughness, bool useShadow)
{
    float3 halfway   = normalize(pixelToEye + lightDir);
    float3 F  = schlickFresnel(F0, max(0.0, dot(halfway, pixelToEye))); // HdotV
    float  D  = ndfGGX(NdotH, roughness);
    float3 G  = schlickGGX(NdotI, NdotO, roughness);
    float3 specularBRDF = (F * D * G) / max(1e-4, 4.0 * NdotI * NdotO);

    float3 kd = lerp(float3(1,1,1) - F, float3(0,0,0), metallic);
    float3 diffuseBRDF = kd * albedo;

    float shadowFactor = useShadow ? getShadowFactor(position, normal) : 1.0;
    shadowFactor = pow(shadowFactor, 3.0);

    float3 radiance = radianceWeight * radianceColor * shadowFactor;
    return (diffuseBRDF + specularBRDF) * radiance * NdotI;
}
```

#### Schlick Fresnel

```hlsl
return F0 + (1 - F0) * pow(2.0, (-5.55473 * VdotH - 6.98316) * VdotH);
```

표준 Schlick 근사 대신 **Epic Games의 최적화 버전**을 사용한다. `pow(2, ...)` 형태로 변환하여 GPU의 `exp2` 명령어를 활용, `pow(x, 5)` 보다 빠르면서 오차가 매우 작다.

#### GGX NDF

```hlsl
float alpha  = roughness * roughness;
float alpha2 = alpha * alpha;
return alpha2 / max(1e-5, PI * pow(NdotH * NdotH * (alpha2 - 1) + 1, 2.0));
```

`roughness²`를 alpha로 사용하는 것은 Disney/Unreal의 관행으로, roughness 값의 **지각적 선형성**을 보장한다. roughness 0.5 → 1.0의 시각적 차이가 0.0 → 0.5의 차이와 유사하게 느껴진다.

#### Schlick-GGX Geometry

```hlsl
float k  = (roughness + 1) * (roughness + 1) / 8;  // Direct Lighting용
float gl = NdotI / (NdotI * (1 - k) + k);           // Light 방향 감쇠
float gv = NdotO / (NdotO * (1 - k) + k);           // View 방향 감쇠
return gl * gv;
```

마이크로면의 Self-Shadowing과 Self-Masking을 모델링한다. IBL용 `k = roughness² / 2`와 달리 Direct Lighting에서는 `k = (roughness+1)² / 8`을 사용한다.

#### Shadow Factor

```hlsl
float shadowValue = percentLit / 10.0;
return shadowValue + (1.0 - shadowValue) * (1.0 - saturate(radianceWeight / maxRadianceWeight));
```

- 1 center sample + 3×3 PCF = 10샘플 평균으로 부드러운 그림자를 생성
- `radianceWeight`가 0에 가까운 밤에는 `shadowFactor`가 1.0으로 수렴 → 빛이 없는 밤의 그림자를 자연스럽게 무효화
- `pow(shadowFactor, 3.0)`으로 그림자 경계를 더 선명하게 만든다

### 4.5 Deferred Shading (ShadingBasicPS.hlsl)

```hlsl
float4 main(psInput input) : SV_TARGET
{
    // 4개 MSAA 샘플 평균
    float3 normal   = avg(normalEdgeTex, 4 samples);
    float3 position = avg(positionTex, 4 samples);
    float3 albedo   = avg(albedoTex, 4 samples);
    float3 mer      = avg(merTex, 4 samples);

    float metallic      = mer.r;
    float roughness     = min(1.0, mer.b + roughnessBias); // roughnessBias = 0.05

    float ao = pow(1.0 - ssaoTex.Sample(...).r, 2.0);

    float3 ambientLighting = getAmbientLighting(ao, albedo, position, normal, metallic, roughness, true);
    float3 directLighting  = getDirectLighting(normal, position, albedo, metallic, roughness, true);

    return float4(clamp(ambientLighting + directLighting, 0.0, 1000.0), 1.0);
}
```

최종 조명은 `Ambient + Direct`의 합산이다. 출력 클램프를 `1000.0`으로 설정하여 **HDR 범위를 유지**한 채 Bloom/Tone Mapping으로 넘긴다.

Non-Edge 픽셀(`main`)은 4샘플 평균으로 1회만 라이팅을 계산하고, Edge 픽셀(`mainMSAA`)은 각 샘플별로 독립 라이팅을 수행한 뒤 평균한다.

### 4.6 전체 파이프라인

```
[CPU - Light.cpp::Update()]
dateTime → 태양 방향, Radiance 색상/세기 → LightConstantBuffer
         → Cascade Shadow ViewProj 행렬  → ShadowConstantBuffer

[GPU - BasicPS.hlsl (G-Buffer Fill)]
blockAtlas  → Albedo
normalAtlas → TBN → World Space Normal
merAtlas    → Metallic / Emission / Roughness
        ↓
G-Buffer: NormalEdge, Position, Albedo, Coverage, MER

[GPU - ShadingBasicPS.hlsl (Deferred Shading)]
G-Buffer 로드 + SSAO + roughnessBias 적용
        ↓
getAmbientLighting(useSkyColor=true)
  ├─ getDiffuseTerm:   kd × albedo × (NdotL × radianceColor + ambientColor)
  └─ getSpecularTerm:  lerp(getSkyColor(reflectDir) × NdotLr, diffuseIrrad, roughness)
                       × (brdfLUT.r × F0 + brdfLUT.g)
        +
getDirectLighting()
  ├─ Diffuse:  kd × albedo
  ├─ Specular: (F × D × G) / (4 × NdotI × NdotO)
  └─ × radianceWeight × radianceColor × shadowFactor × NdotI
        ↓
HDR Output → Post-Processing (Bloom + Tone Mapping)
```

## 5. 문제점 & 해결

### 5.1 IBL 없이 Specular 환경 반사 표현

**문제**: Prefiltered Environment Map 없이 금속/매끈한 표면의 환경 반사를 어떻게 근사할 것인가.

**기존 방식**: `lerp(ambientColor, radianceColor, dot(reflectDir, lightDir))`
— 반사 방향이 태양을 향하면 밝아지고, 반대면 환경색으로 수렴. 태양 방향 1개만으로 표현하므로, 하늘 색상의 변화(노을, 밤하늘)가 반사에 반영되지 않는다.

**개선**: `getSkyColor(reflectDir)` 적용
— 반사 방향으로 절차적 하늘 색상을 직접 계산하므로, 일출/일몰의 하늘 그라디언트와 달 빛이 매끈한 표면에 자연스럽게 반사된다. 큐브맵 없이도 시간대별 환경 반사 변화를 표현한다.

트레이드오프로, 주변 블록이 표면에 반사되는 효과는 표현할 수 없다. 복셀 아트 스타일에서 이 수준의 정밀한 반사는 크게 요구되지 않으며, 태양/달/하늘 하이라이트와 시간대 색상 변화만으로 충분한 금속감을 표현할 수 있다.

### 5.2 낮은 Roughness에서의 Direct Specular 과도 집중

**문제**: 완전한 PBR에서 낮은 roughness 표면은 IBL Specular(환경 반사)가 "거울 느낌"을 담당한다. IBL 없이 Direct Lighting만 존재하면, 낮은 roughness에서 GGX NDF가 극도로 날카로운 Dirac delta 형태에 수렴하여 픽셀 대부분에서 Direct Specular ≈ 0이 되고, 화면에 스페큘러가 거의 보이지 않거나 한 픽셀만 과도하게 밝아진다.

**해결**: `roughnessBias = 0.05`를 더해 최소 roughness를 올린다. NDF의 극단적 수렴을 방지하여 적절한 넓이의 하이라이트를 유지한다. IBL의 에너지를 Direct Specular 억제로 보상하는 방식이다.

### 5.3 밤 시간의 Shadow 아티팩트

**문제**: `radianceWeight`가 0인 밤에도 그림자 계산이 수행되면, PCF 편향이나 부정확한 Depth 비교가 비정상적인 어둡기/밝기를 만들 수 있다.

**해결**: `getShadowFactor()` 반환값에서 `radianceWeight / maxRadianceWeight`를 이용해 밤에는 shadowFactor를 1.0으로 수렴시킨다. 빛이 없는 상황에서 그림자를 자연스럽게 무효화한다.

### 5.4 Shadow SRV/DSV 동시 바인딩 충돌

**문제**: Shadow Map 렌더 패스 종료 후 shadow DSV가 해제되지 않은 상태에서 `PSSetShaderResources`로 shadow SRV를 바인딩하면, D3D11이 같은 리소스의 DSV/SRV 동시 바인딩을 거부한다. 결과적으로 `t11`이 null 상태가 되어 그림자가 완전히 사라진다.

**해결**: `RenderShadowMap()` 종료 직전에 `OMSetRenderTargets(0, nullptr, nullptr)`을 호출하여 shadow DSV를 명시적으로 해제한다. 이후 `SetGlobalLightingSRVs()`에서 shadow SRV가 정상 바인딩된다.

### 5.5 sRGB ↔ Linear 공간 관리

**문제**: CPU에서 정의한 Radiance 색상이 sRGB 공간이지만, 셰이더의 PBR 연산은 Linear 공간을 전제한다.

**해결**: CPU에서 `sRGB → Linear` 변환 후 GPU에 전달한다. 셰이더 내부의 모든 색상 연산은 Linear 공간에서 수행되며, 최종 출력 후 Tone Mapping에서 디스플레이에 맞는 감마 보정을 적용한다.

## 6. 결과

- 한낮에 금속 블록은 선명한 스페큘러 하이라이트를 보이고, 돌/흙 블록은 매트한 Diffuse 반사를 보여 **재질별 차이가 명확하게 표현**된다
- 매끈한 표면에 `getSkyColor`를 통한 하늘 반사가 적용되어, 일출/일몰의 주황색 하늘이 반사면에 자연스럽게 비친다
- 밤에는 Radiance가 0으로 수렴하여 Ambient만 남고, Emission 블록만 빛을 유지한다
- BRDF LUT를 활용한 Split-Sum Approximation으로, 큐브맵 없이도 **roughness에 따른 Specular 변화**가 자연스럽다
- Normal Map 적용으로 평면적인 복셀 면에도 **미세한 요철과 방향성**이 생겨 디테일이 향상된다

## 7. 회고

- IBL을 절차적 하늘 함수(`getSkyColor`)로 대체한 것은 큐브맵 없이 시간대 변화를 반사에 반영하는 실용적인 방법이다. 하지만 주변 지형/블록이 표면에 반사되는 효과는 구현할 수 없다. 이를 위해서는 매 프레임 큐브맵 렌더링 또는 Screen Space Reflection이 필요하다
- roughnessBias는 없는 IBL의 에너지를 Direct Specular 억제로 보상하는 임시방편이다. `getSkyColor` 자체를 roughness에 따라 블러(mip 유사 효과)하는 방식으로 더 물리적으로 올바른 근사를 만들 수 있을 것이다
- Emission 채널이 MER에 존재하지만, Deferred Shading 단계에서 Emission 기반 자체 발광 처리가 별도로 분리되어 있지 않다. Emission 값을 HDR 출력에 더하는 패스를 추가하면 발광 효과를 더 극적으로 표현할 수 있을 것이다
- `pow(ao, 2.0)`, `pow(shadowFactor, 3.0)` 같은 하드코딩된 지수값들은 시각적 튜닝의 결과다. 파라미터화하면 이터레이션이 빨라질 것이다
