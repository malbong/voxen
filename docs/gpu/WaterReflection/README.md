# Water Reflection — Mirror Plane Reflection, Absorption Color, Underwater Filter

<img width="1600" height="800" alt="Image" src="https://github.com/user-attachments/assets/cb4df8b9-fead-4dd2-85bf-645cecb91aaa" />

<img width="1600" height="800" alt="Image" src="https://github.com/user-attachments/assets/3ac57d4e-6dec-4cd1-8cdb-afecc4671db8" />

<img width="1600" height="800" alt="Image" src="https://github.com/user-attachments/assets/709077a8-8815-48ae-93c4-e206af09636c" />

<img width="1600" height="800" alt="Image" src="https://github.com/user-attachments/assets/5d33544f-c2bb-4852-8094-fc067c77b3b6" />

## 1. 개요

Voxen의 수면 렌더링 시스템은 **Planar Reflection(평면 반사)** 기반의 거울 월드 렌더링과, **Beer-Lambert 흡수** 기반의 수중 투영색, **Fresnel 블렌딩**으로 반사-투영을 합성하는 **Forward 렌더링 파이프라인**이다.
수중 상태에서는 렌더링 순서를 변경하고 전용 Color Filter를 적용한다.

핵심 구성:

- **Mirror World** — 수면(Y=64)을 기준으로 반전된 세계를 별도 렌더 타겟에 렌더링
- **Water Color** — 투영색(Beer-Lambert) + 반사색(Mirror) + 수면 자체색(Water Albedo)을 Fresnel로 합성
- **Underwater Filter** — 수중 시 전체 화면에 색 필터 적용

## 2. 도입 동기

수면은 게임 월드에서 가장 넓은 면적을 차지하는 반투명 오브젝트 중 하나다.
Voxel World에서 Depth와 Stencil을 활용한 **수면의 반사, 깊이에 따른 색 변화, 수중에서의 시야 변화**를 적용해보고 싶었다.

## 3. 핵심 아이디어

### 3.1 Planar Reflection (Mirror World)

표면 반사 방식을 사용했다.

장점

- 구현의 난이도가 낮고 직관적이다.
- ViewPort를 넘어가는 오브젝트에 대해서도 반사 렌더링이 가능하다.

단점

- 청크들을 렌더링해야하므로 DrawCall 횟수가 많아져 비효율적이다.
- Draw 비용을 아끼기 위해 낮은 해상도에서 진행하므로 반사가 비현실적으로 뿌옇다

표면 반사를 선택한 이유

- 구현 난이도가 낮음
- SSR에서 Viewport 넘어가는 경우나 Step에 대한 보정 방식에 대한 이해가 없음 -> 경계면의 부자연스러움이 컸음

### 3.2 물 표면 색 결정을 위한 다양한 색 사용

<img width="844" height="427" alt="Image" src="https://github.com/user-attachments/assets/010f7955-b6de-410f-8825-c033288399bd" />

물 표면의 색: Water색(`waterColor`) + 투영렌더색(`projectedColor`) + 반사색(`mirrorColor`)

- Water색(`waterColor`): 시간에 따라 바뀌는 Texture + 온도습도노이즈투영 + PBR 근사
- 투영렌더색(`projectedColor`): DeferredRenderPass의 결과물
- 반사색(`mirrorColor`): Mirror World 렌더링 결과물

### 3.3 수중 렌더링 순서 변경

수면 위와 수면 아래에서 렌더링 순서가 달라야 한다:

- **수면 위**: Mirror World → Water Plane → Fog → Sky → Cloud
- **수면 아래**: Fog → Sky → Cloud → Water Plane

수중에서는 수면이 **위에** 있으므로, 하늘/구름을 먼저 그리고 그 위에 수면을 덮어야 반투명 물질의 올바르게 렌더링 된다.

## 4. 구현 내용

### 4.0 Planar Reflection 구현을 위한 사전 준비

표면에 대한 반사를 렌더링하기 위해 다음과 같이 구성했다.

1. MirrorPlane Reflection Matrix 구성 (`Camera.cpp`)

- MVP 변환 중 M(R)VP가 되고, Model Matrix에 직접 곱하는게 아닌 Camera.View에 곱하여 Constant를 스위칭하는 방식으로 쉐이더에서 사용한다.

2. ChunkManager 에서 ChunkPosition을 반사 후 Frustum Culling

```
Vector3 mirrorChunkPos = Vector3::Transform(chunkPos, camera.GetMirrorPlaneMatrix());
if (FrustumCulling(mirrorChunkPos, camera, light, true, false)) {
	m_renderMirrorChunkList.push_back(p.second);
}
```

3. 960x560 사이즈의 Mirror World RTV 구성

- 성능 문제로 인한 낮은 해상도에 렌더링 버퍼 구성

### 4.1 Mirror World 렌더링 (App.cpp::RenderMirrorWorld)

Mirror World는 4단계로 구성된다:

#### (1) Stencil Masking — 수면이 보이는 영역만 표시

```
// stencil and water depth
{
	Graphics::context->OMSetRenderTargets(
		1, Graphics::mirrorDepthRTV.GetAddressOf(), Graphics::mirrorWorldDSV.Get());
	Graphics::context->PSSetShaderResources(0, 1, Graphics::basicDepthSRV.GetAddressOf());
	Graphics::SetPipelineStates(Graphics::mirrorMaskingPSO);
	ChunkManager::GetInstance()->RenderTransparency();
}
```

Water Plane의 Depth 값을 직접 RTV(`mirrorDepthRTV`)로 설정해 직접 Depth 값을 리턴한다.
`mirrorWorldDSV`로 사용되는 DSV를 MirrorWorld Pass 중 계속 사용하게 되는데, 도중 Plane 자체의 Depth를 SRV(`mirrorDepthSRV`)로 바인딩하여 사용해야 한다.

Mirror World는 낮은 해상도를 사용하기에 G-Buffer에 사용되었던 DSV와 다른 DSV를 사용하게 된다.
G-Buffer에 사용되었던 DSV를 SRV로 바인딩하여 Depth를 비교한다.
G-Buffer에 저장된 Depth 보다 가까운 경우에 Stencil에 표시한다. 그 외는 `discard` 한다.

```
// MirrorMaskingPS.hlsl
Texture2DMS<float4, SAMPLE_COUNT> worldDepth : register(t0);
...
float pixelDepth = input.posProj.z;
int2 base = int2(input.posProj.xy - 0.5) * 2;
float minDepth = 1.0;

[unroll]
for (uint s = 0; s < SAMPLE_COUNT; ++s)
{
    minDepth = min(minDepth, worldDepth.Load(base, s).r);
    minDepth = min(minDepth, worldDepth.Load(base + int2(1, 0), s).r);
    minDepth = min(minDepth, worldDepth.Load(base + int2(0, 1), s).r);
    minDepth = min(minDepth, worldDepth.Load(base + int2(1, 1), s).r);
}

if (minDepth + 1e-4 <= pixelDepth)
    discard;

return pixelDepth;
```

#### (2) Mirror Sky + Mirror Cloud

Skybox는 쉐이더에서 단순히 3D Unit Position Vector의 y값만 뒤집고 렌더링한다.

Skybox — Stencil 마스크 영역에만 그린다.
반전된 큐브맵일 필요가 없으므로 MirrorPlane Matrix는 사용할 필요가 없다.

```
// SkyboxPS.hlsl
float3 posDir = normalize(input.posWorld);
#ifdef USE_MIRROR
    posDir.y *= -1;
#endif
```

Cloud — Stencil 마스크 영역에만 그린다.
반전된 Cloud가 필요하기 때문에 MirrorPlane Matrix를 사용한다.
이후에 원래의 글로벌 Constant Buffer로 재구성한다.
Plane 대칭된 물체는 와인딩이 바뀌기에 RS를 수정하여 PSO에 넣는다.

```
Graphics::context->VSSetConstantBuffers(8, 1, m_camera.m_mirrorConstantBuffer.GetAddressOf());
Graphics::SetPipelineStates(Graphics::cloudMirrorPSO); // mirrorRS (FrontCounterClockwise=true)
m_cloud.Render();

```

- **반사 행렬**: `Plane(Vector3(0, 64, 0), Vector3(0, 1, 0))`로 Y=64 평면 반사. View Matrix에 곱하여 뒤집힌 카메라를 구성
- **mirrorDrawMaskedDSS**: `StencilFunc = EQUAL`, `StencilRef = 1` — Stencil=1인 영역(수면 있는 곳)에만 렌더링
- **mirrorRS**: `FrontCounterClockwise = true` — 반전으로 삼각형 와인딩이 뒤집히므로, Rasterizer의 컬링 방향도 반전

#### (3) Mirror World (블록)

`basicMirrorPSO`를 사용한다:

- `mirrorRS` — 와인딩 반전
- `mirrorDrawMaskedDSS` — Stencil 마스크 영역에만 렌더링
- `basicMirrorPS` — Mirror Depth Texture를 참조하여, 수면보다 아래에 있는 블록만 그린다

Mirror World 물체의 Depth가 Water Plane보다 낮은 경우(물 표면보다 가까이 물체가 있는 경우)는 반사의 대상이 아니기 때문에 제거(`discard`)한다.

```
float4 mainMirror(psInput input) : SV_TARGET
{
    float2 screenTexcoord = float2(input.posProj.x / mirrorWidth, input.posProj.y / mirrorHeight);
    float planeDepth = mirrorDepthTex.Sample(linearClampSS, screenTexcoord).r;
    float pixelDepth = input.posProj.z;

    if (pixelDepth + 1e-4 <= planeDepth) // 거울보다 가까운 미러월드는 필요 없음
        discard;
    ...
}
```

#### (4) Blur

```cpp
m_postEffect.Blur(3, Graphics::mirrorWorldSRV, Graphics::mirrorWorldRTV,
    Graphics::mirrorBlurSRV, Graphics::mirrorBlurRTV, Graphics::blurMirrorPS);
```

Mirror World를 **3회 Blur** 처리한다. 현실의 수면 반사는 완벽한 거울이 아니라 잔물결에 의해 흐릿하므로, Blur로 이를 근사한다.

Mirror World의 해상도는 원본의 절반(`APP_WIDTH/2 × APP_HEIGHT/2`)이다.
반사 이미지에는 높은 해상도가 필요 없고, 어차피 Blur로 흐릿해지므로 절반 해상도로 렌더링하여 성능을 절약한다.

### 4.2 Water Plane — 최종 수면 색상 합성 (WaterPlanePS.hlsl)

<img width="844" height="427" alt="Image" src="https://github.com/user-attachments/assets/010f7955-b6de-410f-8825-c033288399bd" />

물 표면의 색: Water색(`waterColor`) + 투영렌더색(`projectedColor`) + 반사색(`mirrorColor`)

#### Water색(`waterColor`): 시간에 따라 바뀌는 Texture 애니메이션 + 온도습도노이즈투영 + PBR 근사

```
 // 시간에 따라 바뀌는 Texture Index -> 애니메이션
 uint waterStillTextureArraySize = 32;
 uint dateAmountPerSecond = dayCycleAmount / dayCycleRealTime; // 24000 / 30 -> 800
 uint dateAmountPerIndex = dateAmountPerSecond / waterStillTextureArraySize; // 800 / 32 -> 25
 uint waterStillTextureIndex = (dateTime % dateAmountPerSecond) / dateAmountPerIndex;

 // 노멀 매핑 및 cosine에 따라 달라지는 roughness 값
 float3 mappedNormal = normalMapping(input.texcoord, waterStillTextureIndex);
 float roughness = 0.2 / max(dot(mappedNormal, input.normal), 1e-3);

 // WaterAlbedo는 GrassLeafColor와 동일한 노이즈 매핑 방식
 // roughness 값을 이용한 PBR 근사 렌더링을 적용
 float3 albedo = getWaterAlbedo(input.texcoord, waterStillTextureIndex, input.posWorld, mappedNormal);
 float3 ambientLighting = getAmbientLighting(1.0, albedo, input.posWorld, mappedNormal, 0.0, roughness);
 float3 directLighting = getDirectLighting(mappedNormal, input.posWorld, albedo, 0.0, roughness, true);
 float3 waterColor = ambientLighting + directLighting;
```

#### 투영렌더색(`projectedColor`): DeferredRenderPass의 결과물

원래 그려져있던 렌더링 색을 샘플링해옴
SV_Index를 사용하여 비효율적이지만, G-Buffer에서 사용한 `positionTex`도 사용하기에 `msaaRenderTex` 그대로 사용한다.

```
float3 projectedColor = msaaRenderTex.Load(input.posProj.xy, sampleIndex).rgb;
```

#### 흡수 계산: eyeToWaterPlaneColor = Water색(`waterColor`) + 투영렌더색(`projectedColor`)

`waterColor`와 `projectedColor`가 적절히 섞여야 할 필요가 있다.

Water Plane 표면에서 투영될 물체까지의 거리를 이용하여 투과되는 거리를 계산한 후 Beer-Lambert 법칙에 근사한다.
coeff는 적절히 렌더링 결과를 보고 상수로 지정한다.

```
float planeToProjectionObjectDistance = length(input.posWorld - projectedObjectPosition);
float waterAbsorptionCoeff = 0.075;
float waterAbsorptionFactor = 1.0 - exp(-waterAbsorptionCoeff * planeToProjectionObjectDistance); // beer-lambert
```

이 흡수율(`waterAbsorptionFactor`)을 이용하여 Water 자체의 색과 투영렌더색을 혼합하여 저장한다 (`eyeToWaterPlaneColor`)

```
float3 eyeToWaterPlaneColor = lerp(projectedColor, waterColor, waterAbsorptionFactor);
```

#### Fresnel 효과를 고려한 반사율 계산 후 최종 색 결정

`eyeToWaterPlaneColor` 는 단순히 물과 투영되는 물체간의 색이다.
Mirror World 색을 섞을 필요가 있다. 반사율을 단순히 계산하기 위해 schlick Fresnel 근사를 사용한다.
수직에 가까워지면 반사율은 1, 정면을 바라보는 경우 F0 상수값 그대로 사용하게 될 것이다.

```
float3 schlickFresnel(float3 N, float3 E, float3 F0)
{
    // [f0 ~ 1]
    // 90 -> dot(N,E)==0 -> f0+(1-f0)*1^5 -> 1
    //  0 -> dot(N,E)==1 -> f0+(1-f0)*0*5 -> f0
    return F0 + (1 - F0) * pow((1 - max(dot(N, E), 0.0)), 5.0);
}

// fresnel factor
float3 planeToEye = normalize(eyePos - input.posWorld);
float3 reflectCoeff = float3(0.02, 0.02, 0.02); // F0
float3 fresnelFactor = schlickFresnel(mappedNormal, planeToEye, reflectCoeff);

// blending 3 colors
float3 blendColor = lerp(eyeToWaterPlaneColor, mirrorColor, fresnelFactor);
return float4(blendColor, 1.0);
```

### 4.3 Underwater Filter

수중 상태일 때 전체 화면에 푸른 색 필터(`WaterFilterPS.hlsl`)를 적용한다.
단순히 화면 전체에 색을 입히는 과정이다.

### 4.4 렌더링 순서 분기

```cpp
// Forward Render Pass
if (m_camera.IsUnderWater()) {
    RenderFogFilter();
    RenderSkybox();
    RenderCloud();
    RenderWaterPlane();     // 마지막: 수면이 위에 있으므로
}
else {
    RenderMirrorWorld();    // 수면 위에서만 Mirror 필요
    RenderWaterPlane();     // 먼저: 수면이 아래에 있으므로
    RenderFogFilter();
    RenderSkybox();
    RenderCloud();
}

// Post Effect
if (m_camera.IsUnderWater()) {
    RenderWaterFilter();    // 수중 전용 색 필터
}
...
```

**수면 위 순서**: Mirror World → Water Plane → Fog → Sky → Cloud

- Mirror World를 먼저 렌더링하여 반사 텍스처를 준비
- Water Plane에서 반사 텍스처와 투영을 합성
- Fog/Sky/Cloud는 수면 위에 그려져 수면 뒤의 배경이 됨

**수중 순서**: Fog → Sky → Cloud → Water Plane

- 수중에서는 Mirror World 불필요 (반사 없음)
- Fog를 먼저 적용하여 수중 기하에 안개 효과
- Sky/Cloud를 그린 뒤 마지막에 Water Plane을 덮어 수면의 반투명 효과
- Post Effect에서 Water Filter로 전체 화면에 청색 필터

### 4.5 전체 파이프라인 요약

```
[수면 위]
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

(1) Mirror World (절반 해상도)
    Stencil Masking (수면 영역) → Mirror Sky → Mirror Cloud → Mirror Block (반전 카메라) → Blur (3회)
        ↓ mirrorWorldSRV
(2) Water Plane 색 결정
    입력: originColor(기존 렌더), mirrorColor, positionTex
        ↓
    basicMSRTV


[수중]
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

(1) Fog Filter (가시거리 단축)
(2) Sky + Cloud
(3) Water Plane: lerp(origin, waterAlbedo, 0.5)
(4) Water Filter: 전체 화면 색 필터 적용
```

## 5. 문제점 & 해결

### 5.0 반사 렌더링 방식 고려

**문제**: Reflection 렌더링 방식의 다양한 방법이 존재했고 여러가지 고려해야 했다.

- 단순 환경맵 샘플링
- Planar Reflection
- Screen Space Reflection
- RayTracing

**해결**

- 환경맵 샘플링은 동적인 환경맵 Skybox에서 큐브맵을 텍스쳐에 구워 SRV로 샘플링하는건 너무 비효율적이고 물체 반사를 구현하지 못한다.
- SSR은 ViewPort를 넘어가는 문제에 대해서 해결방법을 고려하지 못했다.
- RayTracing, SSR은 복잡성이 있어보였고 구현 난이도가 있어 따로 프로젝트를 해야할 것 같았다.
- 사실-성능-구현난이도 등의 트레이드오프를 고려하면 Planar Reflection이 가장 적합해 보여서 선택했다.

### 5.1 수면 가려짐 처리 (Mirror Masking)

**문제**: 수면이 불투명 블록 뒤에 있는 경우에도 반사가 그려지면 부자연스럽다.

**해결**: `MirrorMaskingPS`에서 G-Buffer의 Position 깊이와 수면 깊이를 비교하여, **블록에 가려진 수면 픽셀은 discard**한다. Stencil에 기록되지 않으므로 해당 영역에는 Mirror World가 렌더링되지 않는다.

### 5.2 Mirror World에서의 Pixel Shader

**문제**: 원래의 Shader와 Mirror World의 Shader 공통점과 차이점으로 인해 분기 필요

**해결**: 쉐이더를 따로 만들지 않고 매크로나 main 진입점을 따로 잡아 쉐이더를 컴파일하여 사용했다.

### 5.3 Mirror World의 성능

**문제**: 전체 세계를 한 번 더 렌더링하는 것은 비용이 크다.

**해결**:

- **절반 해상도** (`MIRROR_WIDTH = APP_WIDTH/2`) — 반사 이미지는 Blur로 흐릿해지므로 절반 해상도로 충분
- **Stencil 마스킹** — 수면이 보이는 영역에만 렌더링하여, 화면 대부분이 수면이 아닌 경우 대폭 절약
- **Low-LOD 블록만 렌더링** (`RenderMirrorWorld`) — 반사에는 디테일이 불필요하므로 Low-LOD 메시 사용
- **수중에서는 Mirror 렌더링 생략** — 반사가 보이지 않으므로 완전히 건너뜀
- **MirrorChunk FrustumCulling** — View Frustum에 걸리지 않는 Mirror World Chunk는 제외하고 Draw함

### 5.4 MirrorMasking Stencil에서의 깜빡임 문제

**문제**: Mirror World는 절반 해상도(`MIRROR_WIDTH × MIRROR_HEIGHT`)에서 렌더링되지만, G-Buffer depth(`basicDepthSRV`)는 원본 해상도(`APP_WIDTH × APP_HEIGHT`)다.
`MirrorMaskingPS`에서 `SV_POSITION`으로 들어오는 `posProj.xy`는 Mirror 공간 좌표(0 ~ mirrorWidth)이므로, 이를 G-Buffer 텍스처의 정수 픽셀 좌표로 변환하려면 ×2 스케일이 필요하다.

**해결**: Mirror 픽셀 하나에 대응하는 full-res 2×2 블록 전체의 depth 최솟값을 사용한다.

```hlsl
// MirrorMaskingPS.hlsl
int2 base = int2(input.posProj.xy - 0.5) * 2; // 2×2 블록 좌상단 픽셀
float minDepth = 1.0;

[unroll]
for (uint s = 0; s < SAMPLE_COUNT; ++s)
{
    minDepth = min(minDepth, worldDepth.Load(base,                s).r);
    minDepth = min(minDepth, worldDepth.Load(base + int2(1, 0),   s).r);
    minDepth = min(minDepth, worldDepth.Load(base + int2(0, 1),   s).r);
    minDepth = min(minDepth, worldDepth.Load(base + int2(1, 1),   s).r);
}

if (minDepth + 1e-4 <= pixelDepth)
    discard;
```

**참고**: 해상도가 1:1이 아닌 것과, Depth 정밀도로 인한 일부가 깜빡이는 문제는 제거할 수 없다.
Blur와 현재 프로젝트로 충분히 괜찮은 결과다.

## 6. 회고

- 현재 Planar Reflection은 **평면 수면(Y=64)** 에만 적용 가능하다. 폭포나 파도처럼 수면이 평면이 아닌 경우에는 SSR(Screen-Space Reflection)이나 Ray-Traced Reflection이 필요하다.
  - Water Plane이 **평면 수면(Y=64)**에 하드 코딩되어 있고, 다른 곳에 물이 위치하는 경우 렌더링이 안되는 단점이 있다.
  - 이는 단순히 물이 수면위치에 고정되어 있다고 가정하고 작성한 로직이다.
  - 만약 물이 다른 곳에 위치하거나, 반사가 되는 다른 메쉬가 존재하면 해당 반사는 변경될 필요가 있다.
