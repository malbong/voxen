# Lighting PBR — 동적 태양광 기반 PBR 라이팅

<img width="1920" height="1076" alt="Image" src="https://github.com/user-attachments/assets/61f867c4-c6b7-4bdc-b20f-0af94d5e11db" />

<img width="1916" height="1075" alt="Image" src="https://github.com/user-attachments/assets/f6a5388f-af15-49f5-92f2-c425b04b84fc" />

<img width="1911" height="1074" alt="Image" src="https://github.com/user-attachments/assets/632a2bec-fc12-4d73-b7d0-017d89baf698" />

## 1. 개요

Voxen의 라이팅 시스템은 **동적으로 회전하는 태양**을 단일 Directional 광원이다.

**Unreal Engine 스타일의 PBR(Physically Based Rendering)** 을 근사하여 재구성한 쉐이딩이다.

G-Buffer에 기록된 Normal Map, MER(Metallic/Emission/Roughness) 텍스처 정보를 Deferred Shading 단계에서 읽어 물리 기반의 Ambient + Direct Lighting을 계산한다.

핵심 구성 요소:

- **CPU (Light.cpp)** — 게임 시간에 따른 태양 방향, Radiance 색상/세기 계산 + Cascade Shadow Map 행렬 갱신
- **GPU (Lighting.hlsli)** — Schlick Fresnel, GGX NDF, Schlick-GGX Geometry, IBL 근사, Shadow 함수
- **GPU (ShadingBasicPS.hlsl)** — G-Buffer 로드 후 Ambient + Direct Lighting 합산

## 2. 도입 동기

Full Forward Rendering Pass에서 SSAO 구현의 이유로 Deferred Rendering Pass를 구성했으므로 이에 따른 라이팅 연산도 스크린 스페이스에서 연산할 수 있어서 비용에 큰 부담이 없었다.

추가로 Unreal PBR(2014)에 대해서 학습했었고 구현할 수 있는 계기가 되었다.

다른 렌더러와는 달리 폴리곤이 단순하여 PBR의 결과가 조금 아쉬울 순 있어도 단순 Phong 모델보다는 나을 것이라는 생각으로 도입했다.

## 3. 핵심 아이디어

### 3.1 sRGB to Linear + HDR

물리 기반 연산을 하기 때문에 sRGB를 Linear Space Color로 가져와야했다.

- 직접 CPU에서 색을 결정할 때도 많았기에 이를 모두 sRGB->Linear로 옮겨서 GPU에 전달한다.
- 다른 이미지들도 SRGB 포맷으로 저장하여 샘플링 시 Linear Space로 가져와서 연산했다.
- 이후에 쉐이딩 이후 Bloom -> Combine 과정에서 선형 톤맵핑을 진행하면서 Gamma Correction을 한다.

자연스레, FLOAT16-32을 사용하여 HDR이 되었다.

- 이는 연산할 수 있는 색의 범위가 늘어난다.

### 3.2 Unreal PBR 모델 차용

Epic Games의 ["Real Shading in Unreal Engine 4"](https://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf) 에서 제시한 Cook-Torrance Specular BRDF를 기반으로 한다.

```
Cook-Torrance Specular BRDF = (F · D · G) / (4 · NdotI · NdotO)
```

| 항목                        | 함수                  | 설명                                   |
| --------------------------- | --------------------- | -------------------------------------- |
| **F** (Fresnel)             | Schlick Approximation | 시야각에 따른 반사율 변화              |
| **D** (Normal Distribution) | GGX/Trowbridge-Reitz  | 마이크로면의 법선 분포                 |
| **G** (Geometry)            | Schlick-GGX           | 마이크로면 자기 그림자(Self-Shadowing) |

Diffuse는 에너지 보존을 위해 `kd = (1 - F) × (1 - metallic)`로 가중하여, 금속일수록 Diffuse 기여가 줄고 Specular 기여가 지배적이 된다.

### 3.3 IBL 없는 환경에서의 Ambient 근사

DirectLighting은 단순히 BRDF 함수를 이용하여 렌더링할 수 있었지만 AmbientLighting은 모든 면에서 오는 빛에 대한 처리가 까다로웠다.

특히 Unreal PBR은 Ambient Lighting 시에 **IBL(Image Based Lighting)** 을 사용하는 환경이다.

하지만, 내 프로젝트에서는 동적인 환경에 대한 IBL이 존재하지 않는다.

동적인 환경에서 Skybox를 직접 쉐이더에서 찍어내고 있고, 이것을 다시 Cubemap에 구워 IBLMap을 만들기엔 느리다고 생각했다.

그래서 Ambient에서 Diffuse, Specular에 대한 IBL을 대략적으로 비슷하게 근사하였다.

1. Diffuse Irradiance: N과 L의 각도를 기준으로 임의 구성
2. Specular Irradiance: 반사(R) 방향의 SkyColor -> roughness가 높으면 Diffuse Term과 유사하게 구성

| IBL 구성 요소           | Unreal 원본                                 | 내 프로젝트 근사                                              |
| ----------------------- | ------------------------------------------- | ------------------------------------------------------------- |
| **Diffuse Irradiance**  | Irradiance Cubemap 샘플링                   | `(radianceColor × NdotL) + ambientColor`                      |
| **Specular Irradiance** | Prefiltered Environment Map (roughness→mip) | `lerp(getSkyColor(reflectDir), diffuseIrradiance, roughness)` |
| **Split-Sum**           | Pre-integrated BRDF LUT                     | 동일하게 `brdfTex` 사용                                       |

```
IBL Specular 근사:
  낮은 roughness → getSkyColor(reflectDir)로 하늘을 직접 반사 (거울 느낌)
  높은 roughness → diffuseIrradiance로 수렴 (Lambertian에 가까운 확산)

  lerp(reflectionColor × reflectRadianceWeight, diffuseIrradiance, roughness)
```

## 4. 구현 내용

### 4.1 CPU: 태양 방향과 Radiance 계산 (Light.cpp)

`m_dir`: 태양쪽으로 바라보는 방향, 이는 단순히 Update문에서 중심축 기준 시간에 따라 회전한다.

```cpp
float angle = (float)dateTime / Date::DAY_CYCLE_AMOUNT * 2.0f * PI;

Matrix rotationAxisMatrix = Matrix::CreateFromAxisAngle(
    Vector3(-cos(PI/4), 0.0f, cos(PI/4)), angle);

m_dir = Vector3::Transform(
    Vector3(cos(PI/4), 0.0f, cos(PI/4)), rotationAxisMatrix);
```

`m_radianceWeight`: 태양의 고도를 기준으로 [0, MAX_RADIANCE_WEIGHT(2.5)] 결정된다.

```cpp
// 태양 고도 기반 Smootherstep으로 세기 결정
float w = (currentAltitude - nightEndAltitude) / (1.0f - nightEndAltitude);
m_radianceWeight = Smootherstep(0.0f, MAX_RADIANCE_WEIGHT, w);
```

`m_radianceColor`: 태양 자체의 색. 시간에 따라 결정된다.

```
DAY(1000~11000)  →  SUNSET(~12500)  →  NIGHT(13700~22300)  →  SUNRISE(~23500)  →  DAY
   White(1,1,1)     Orange(0.64,0.26,0.04)  Black(0,0,0)      Gold(0.72,0.60,0.34)
```

### 4.2 G-Buffer 기록: Normal Map과 MER 텍스처 (BasicPS.hlsl)

cf. 에셋은 https://www.curseforge.com/minecraft/texture-packs/vanilla-pbr 에서 구할 수 있었다.

#### Normal Mapping

복셀 특성상 한쪽 면은 항상 축 정렬되기에, 별도의 추가 Vertex 데이터 없이 **노멀로 Tangent를 직접 결정**한다.

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

#### MER (Metallic / Emission / Roughness) 텍스처

| Atlas                     | Register | 역할                                   |
| ------------------------- | -------- | -------------------------------------- |
| `blockAtlasTextureArray`  | t0       | Albedo                                 |
| `normalAtlasTextureArray` | t1       | Normal Map (Tangent Space)             |
| `merAtlasTextureArray`    | t2       | R: Metallic, G: Emission, B: Roughness |

### 4.3 Lighting (Lighting.hlsli)

`Color = DirectLighting + AmbientLighting` 으로 구성된다.

1. DirectLighting은 점으로 들어오는 빛에 대한 BRDF 함수를 직접 실행하여 값을 결정하면 된다.

- 이 때도 Diffuse, Specular가 나뉘는데, Diffuse BRDF는 단순히 상수(Albedo)로 사용한다. 에너지 보존 kd, ks를 사용하여 감쇄한다.
- Specular는 Cook-Torrance Model을 사용한다: `(F · D · G) / (4 · NdotI · NdotO)`
- F, D, G 함수는 Unreal Course Note에서 사용하는 함수를 직접 사용한다.

2. AmbientLighting은 점으로 들어오는 모든 방향에서 오는 빛에 대한 BRDF 함수를 실행해야하는데, 이는 IBL로 구성한다.

- 현재, IBL을 구성할 수 없으므로 IBL을 근사하여 표현한다.

### 4.4 Direct Lighting

#### specular BRDF

Cook-Torrance Model을 그대로 사용한다.

<img width="689" height="835" alt="Image" src="https://github.com/user-attachments/assets/6a75a643-acbc-489a-bc8b-2bda0f152954" />

```
const float3 Fdielectric = 0.04;
float3 F0 = lerp(Fdielectric, albedo, metallic);
float3 F = schlickFresnel(F0, max(0.0, dot(halfway, pixelToEye))); // HoV
float D = ndfGGX(NdotH, roughness);
float3 G = schlickGGX(NdotI, NdotO, roughness);

float3 specularBRDF = (F * D * G) / max(1e-4, 4.0 * NdotI * NdotO);
```

1. F: Schlick Fresnel

- 보는 각도에 따라서 색이나 밝기가 달라지는 함수
- 여기서는 dot(N, V)가 아닌 dot(H, V)를 사용한다.
- WaterPlane에서 사용한 표준 Schlick 근사 대신 **Epic Games의 버전**을 사용한다.

```hlsl
return F0 + (1 - F0) * pow(2.0, (-5.55473 * VdotH - 6.98316) * VdotH);
```

2. D: GGX NDF

- Microfacet에서, 보는 방향이 Normal인 미세 표면의 비율을 의미하는 함수
- 가장 중요한 영향을 미치는 함수고 그 만큼 roughness가 많은 영향을 미친다.

```hlsl
float alpha  = roughness * roughness;
float alpha2 = alpha * alpha;
return alpha2 / max(1e-5, PI * pow(NdotH * NdotH * (alpha2 - 1) + 1, 2.0));
```

3. G: Schlick-GGX Geometry

- Shadowing & Masking과 관련된 함수
- 미세 표현이 기하에 의해 가려진다고 판단하는 함수

```hlsl
float k  = (roughness + 1) * (roughness + 1) / 8;  // Direct Lighting용
float gl = NdotI / (NdotI * (1 - k) + k);           // Light 방향 감쇠
float gv = NdotO / (NdotO * (1 - k) + k);           // View 방향 감쇠
return gl * gv;
```

#### diffuse BRDF

Diffuse는 기본적으로 상수 값을 사용한다.

<img width="663" height="173" alt="Image" src="https://github.com/user-attachments/assets/404528a6-23fd-4534-98d3-79cb11d560fd" />

```
float3 kd = lerp(float3(1, 1, 1) - F, float3(0, 0, 0), metallic);
float3 diffuseBRDF = kd * albedo;
```

- cf. PI는 반구 전체에서 적분했을 때 에네지 보존을 위해 PI를 나눠서 사용
  - `cdiff​` 자체가 Albedo -> 입사된 빛에 대한 반사 빛의 세기
    - 반구 전체에 대한 적분 -> 입사된 빛 -> PI를 나누지 않으면 albedo x PI가 되버림
  - 직접광에서는 BRDF x L 에서 L의 강도에 PI를 곱해서 사용했다고 가정하여 단순히 Albedo만 사용
  - 빛의 강도는 `NdotI`를 이후에 곱해서 사용되므로 문제 없음

#### Shadow Factor

```hlsl
float shadowValue = percentLit / 10.0;
return shadowValue + (1.0 - shadowValue) * (1.0 - saturate(radianceWeight / maxRadianceWeight));
```

- 1 center sample + 3×3 PCF = 10샘플 평균으로 부드러운 그림자를 생성
- `radianceWeight`가 0에 가까운 밤에는 `shadowFactor`가 1.0으로 수렴 → 빛이 없는 밤의 그림자를 자연스럽게 무효화
- `pow(shadowFactor, 3.0)`으로 그림자 경계를 더 선명하게 만든다

### 4.5 Ambient Lighting (Lighting.hlsli)

마찬가지로 Specular와 Diffuse를 나누어 생각한다.

이 때, IBL을 사용하지 않으므로 IBL에 적절하다고 생각되는 값으로 직접 결정한다.

기본적으로 가지는 환경광에 대해서 결정한다.

#### Basic Ambient Color

이는 기본적인 환경광이 될 것이다.

태양 고도가 낮아질수록 Horizon Color로 전환한다.

- 카메라가 태양 방향을 바라볼수록(`sunAniso`) 따뜻한 `sunHorizonColor` 비중이 증가해, 일몰 시 태양 방향의 Ambient가 붉게 물든다.

```hlsl
float3 getBasicAmbientColor()
{
    float3 ambientColor = float3(1.0, 1.0, 1.0);

    float sunAltitude = lightDir.y;
    float dayAltitude = sin(PI / 24.0);
    if (sunAltitude <= dayAltitude)
    {
        float sunAniso = max(dot(lightDir, eyeDir), 0.0);
        float3 eyeHorizonColor = lerp(normalHorizonColor, sunHorizonColor, sunAniso);

        float maxHorizonColorAltitude = -sin(PI / 24.0);
        float altitudeWeight = smoothstep(maxHorizonColorAltitude, dayAltitude, sunAltitude);

        ambientColor = lerp(eyeHorizonColor, ambientColor, altitudeWeight);
    }

    float basicAmbientWeight = 0.5;

    return ambientColor * basicAmbientWeight;
}
```

#### Diffuse Term

Specular Irradiance 근사:

- 기본적으로 들어오는 환경광`getBasicAmbientColor`을 기본적으로 사용한다.
- 노멀과 태양의 방향이 가까울 수록 직접광의 색(`radianceColor`)에 대한 색을 더한다.
- Roughness는 사용되지 않는데, 반구에 들어오는 빛을 모을 때 roghness에 무관하게 빛이 들어올 것이라는 가정이다.

`kd`는 에너지 보존 가중치로, Fresnel 반사가 많을수록 Diffuse 에너지가 줄어든다.

- `metallic = 1.0`이면 `kd = 0`이 되어 Diffuse가 완전히 사라진다.
- `metallic`이 높을 수록 Specular가 커지기에 에너지 보존을 한다.

```hlsl
float3 getDiffuseTerm(float3 albedo, float3 pixelToEye, float3 normal, float metallic)
{
    float3 Fdielectric = float3(0.04, 0.04, 0.04);
    float3 F0 = lerp(Fdielectric, albedo, metallic);
    float3 F = schlickFresnel(F0, max(0.0, dot(normal, pixelToEye)));

    float3 kd = lerp(1.0 - F, 0.0, metallic);

    float3 diffuseIrradiance = (radianceColor * max(dot(normal, lightDir), 0.0)) + getBasicAmbientColor();

    return kd * albedo * diffuseIrradiance;
}
```

#### Specular Term

Specular Irradiance(pre-Filtered) 근사:

- 기본적으로 Specular Irradiance는 roughenss에 따라 mipmap으로 샘플링한다.
- 이는 roughness가 낮으면 정반사에 가깝고, roughness가 높으면 난반사에 가깝다는 것이다.
- 이를 토대로, roughness가 낮으면 반대 방향의 하늘을 비추고, roughness가 높으면 diffuseIrradiance와 똑같은 식을 사용한다.

**Split-Sum Approximation**

- `brdfTex`는 사전 계산된 2D LUT로, `(NdotV, 1-roughness)` 입력에서 `(scale, bias)`를 반환한다.
- 이는 IBLBacker에서 LUT를 직접 가져와 Asset으로 사용한다.

```hlsl
float3 getSpecularTerm(float3 albedo, float3 pixelToEye, float3 normal, float metallic, float roughness, bool useSkyColor)
{
    float3 basicAmbientColor = getBasicAmbientColor();
    float3 diffuseIrradiance = (radianceColor * max(dot(normal, lightDir), 0.0)) + basicAmbientColor;

    float3 reflectDir = normalize(reflect(-pixelToEye, normal));
    float3 reflectRadiance = useSkyColor ? getSkyColor(reflectDir) : basicAmbientColor;

    float3 specularIrradiance = lerp(reflectRadiance, diffuseIrradiance, roughness);

    float3 Fdielectric = float3(0.04, 0.04, 0.04);
    float3 F0 = lerp(Fdielectric, albedo, metallic);
    float2 specularBRDF = brdfTex.Sample(pointClampSS, float2(dot(pixelToEye, normal), 1 - roughness)).rg;

    return specularIrradiance * (specularBRDF.r * F0 + specularBRDF.g);
}
```

### 4.6 Deferred Shading (ShadingBasicPS.hlsl)

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

    float3 lighting = ambientLighting + directLighting;
    return float4(lighting, 1.0);
}
```

최종 조명은 `Ambient + Direct`의 합산이다.

Non-Edge 픽셀(`main`)은 4샘플 평균으로 1회만 라이팅을 계산하고, Edge 픽셀(`mainMSAA`)은 각 샘플별로 독립 라이팅을 수행한 뒤 평균한다.

- Edge 픽셀에 대해서 SampleWeight를 사용하지 않는다.
- 해당 프로젝트 특성상 하나의 블록 면에 16x16 픽셀 텍스쳐를 사용하기에, Coverage가 같더라도 다른 Albedo나 Normal을 사용할 수 있기 때문이다.

### 4.7 전체 파이프라인

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
getAmbientLighting()
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

### 5.1 roughness가 작을 때의 렌더링

**문제**: roughness가 작을 때, 매끄러운 재질로 쨍한 느낌의 렌더링이 되어야 하는데 그러지 못했다.
— DirectLighting의 Specular BRDF에서 D값이 뾰족하게 수렴하여 한두개의 픽셀만 쨍해진다.
— 또한, IBL에 따라 매끄럽게 반사했어야 했는데 단순히 **문제 5.2**처리로 인해, 반사에 대한 IBL 근사를 하지 않고 있어 그런 모습이 보이지 않았다.

**해결**:
— Roughness Bias를 더해줘서 내 Lighting 모델에 더 적합한 결과를 얻을 수 있었다.
— Specular IBL을 정밀하게 근사해야겠다고 생각했다.

### 5.2 Specular Irradiance Map 근사

**문제**: 큰 고민 없이 결정한 SpecularIrradiance 근사 값 `lerp(ambientColor, radianceColor, dot(reflectDir, lightDir))`
— 반사 방향이 태양을 향하면 밝아지고, 반대면 환경색으로 수렴.
— 기존 방식에서는 단순히 반사벡터가 태양 방향에 가까우면 직접광 색을 그대로 사용하고 그렇지 않으면 주변광을 샘플링했으나 올바르지 않은 근사값이였다.
— 특히, 금속이나 매끈한 재질에서 태양이 렌더링되지 않았다.

**해결**: `getSkyColor(reflectDir)` 적용
— 반사 방향으로 하늘 색상을 직접 계산하므로, 일출/일몰의 하늘 그라디언트와 태양빛/달빛이 매끈한 표면에 자연스럽게 반사된다.
— roughness가 작아도 Ambient Specular에서 충분히 보간되어 큰 문제를 해결했다.

### 5.3 Edge 픽셀에서의 SampleWeight 연산

**문제**: Edge 픽셀에서 Coverage가 같은 샘플을 묶어 한번에 처리하니 같은 블록 면에 다른 색이 맺혀야하는 부분이 원하지 않는 렌더링 결과가 나타났다.

**해결**: SampleWeight를 사용하지 않음.
— 비용적 트레이드가 존재했으나, 16x16 블록면을 올바르게 렌더링하기 위해서는 꼭 했어야 하는 방식이였다.

### 5.4 여기저기 필요한 Lighting 함수

**문제**: Lighting PBR 로직을 사용하는 쪽이 Deferred Shading뿐만 아니라 Forward Shading에서 필요했다.

**해결**
— `Common.hlsli`에 Lighting 로직을 작성하고 필요한 곳에서 포함하여 사용했다.
— 추가로 Lighting 로직이 커짐에 따라 `Lighting.hlsli`로 다시 분리하여 사용했다.

### 5.5 Lighting.hlsli에 필요한 Texture SRV

**문제**: Lighting에 필요한 brdf, shadow, sun 등의 SRV가 필요했고 바인딩을 언제 해주고 풀어줘야하는지 문제가 있었다.

**해결**
— 일반적으로 SRV를 샘플링 없이 바인딩되는 경우 비용이 현저히 적다고 한다.
— ShadowMap Pass에서만 바인딩을 풀어주고, 끝나면 바인딩을 다시하고 파이프라인을 실행시켰다.
— 이 때, ShadowMap Pass가 끝나도 RTV가 ShadowBuffer라 SRV 바인딩이 되지 않았는데, ShadowMap Pass가 끝나면 RTV를 nullptr 수정했다.

## 6. 회고

- Unreal PBR 식 자체를 이해하는 것도 어려웠지만 실제로 비슷하게 구현하려고 노력했던 챕터
- Lighting이 마음에 들지 않을 때 Ambient이 문제였는지 Direct 문제였는지, 또 거기서 Diffuse가 문제인지 Specular가 문제인지 판단하기 어려웠다.
- 완벽한 이해는 아니지만 수식이 어떻게 코드로 옮겨지는지 볼 수 있는 PBR Lighting이였다.
  - 하지만 수식과 코드의 갭 차이를 혼자 메꾸기엔 아직 역부족이라고 느꼈다.
- 단순한 사각 메쉬가 많은 voxel이라 그런지, 아니면 IBL 없이 근사해서 그런지 렌더링 결과는 단순히 PBR에 집중한 정적 렌더러보다 아쉬웠다.
- PBR에 사용되는 IBL을 직접 구워 보고 싶었고, IBLBacker 등 Tool을 이용한 IBL과 비교해보고 싶다는 생각이 들었다.
