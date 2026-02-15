# Deferred Shading with MSAA — Stencil 기반 Edge/NonEdge 분기 처리

## 1. 개요

Deferred Shading 파이프라인에서 MSAA 안티에일리어싱을 효율적으로 처리하는 시스템이다. 전체 화면을 4x MSAA로 셰이딩하는 대신, **Edge Masking** 단계에서 실제 에지인 픽셀만 Stencil로 마킹하고, 이후 SSAO와 Shading 패스를 Edge/NonEdge 두 번에 나누어 실행한다. NonEdge 픽셀은 단일 샘플로 가볍게 처리하고, Edge 픽셀만 멀티 샘플로 처리하여 품질과 성능의 균형을 맞춘다.

## 2. 도입 동기

Deferred Shading과 MSAA는 근본적으로 상충한다. G-Buffer에는 여러 MRT(Multi Render Target)가 4x MSAA로 기록되는데, Shading 단계에서 모든 픽셀의 4개 샘플을 각각 라이팅 연산하면 셰이딩 비용이 4배가 된다. 화면의 대부분은 단일 오브젝트 위에 있어 모든 샘플이 동일한 값을 가지므로, 이런 픽셀에서 4배 연산은 낭비다.

핵심 관찰은 **MSAA가 실제로 필요한 픽셀은 오브젝트 경계(Edge)에만 존재한다**는 것이다. Edge가 아닌 픽셀은 4개 샘플이 모두 같으므로 1번만 셰이딩하면 충분하다.

## 3. 핵심 아이디어

Stencil Buffer를 사용해 화면을 Edge(=1)와 NonEdge(=0) 영역으로 분류한 뒤, 동일한 렌더 타겟에 두 번의 Draw Call로 각 영역을 처리한다.

```
[G-Buffer Fill]  4x MSAA로 기하 정보 기록 (SV_COVERAGE로 에지 감지 준비)
       ↓
[Edge Masking]   Stencil에 에지 마킹 (Stencil = 1)
       ↓
[SSAO]           Stencil == 0 → main()       (샘플 0번 1회)
                 Stencil == 1 → mainMSAA()   (유효 샘플별 가중 연산)
       ↓
[Shading]        Stencil == 0 → main()       (4샘플 평균 1회)
                 Stencil == 1 → mainMSAA()   (유효 샘플별 개별 라이팅)
       ↓
[ConvertToMSAA]  Non-MSAA → MSAA 버퍼 복사
       ↓
[Forward Pass]   Skybox, Cloud, Water, Fog (MSAA 버퍼에 직접 렌더링)
```

## 4. 구현 내용

### 4.1 G-Buffer 구성 (FillGBuffer)

G-Buffer는 5개의 4x MSAA 렌더 타겟으로 구성된다.

| G-Buffer | 포맷 | 내용 |
|---|---|---|
| NormalEdge | R16G16B16A16_FLOAT | RGB: 월드 노멀, A: Edge 플래그 |
| Position | R32G32B32A32_FLOAT | RGB: 월드 위치, A: 유효성 (-1이면 빈 픽셀) |
| Albedo | R16G16B16A16_FLOAT | RGB: 베이스 컬러 |
| Coverage | R32_UINT | SV_COVERAGE 비트마스크 |
| MER | R32G32B32A32_FLOAT | R: Metallic, G: Emission, B: Roughness |

BasicPS.hlsl에서 `SV_COVERAGE` 시맨틱을 이용해 에지 여부를 판별한다:

```hlsl
psOutput main(psInput input, uint coverage : SV_COVERAGE, uint sampleIndex : SV_SampleIndex)
{
    bool edge = (coverage != 0xf);  // 0b1111이 아니면 → 일부 샘플만 덮음 → 에지
    output.normalEdge = float4(normalize(normal), float(edge));
    output.coverage = coverage;
    ...
}
```

`SV_COVERAGE`는 4x MSAA에서 해당 프리미티브가 어떤 서브 샘플을 덮는지를 4비트 마스크로 나타낸다. `0xf`(= 0b1111, 4개 모두 덮음)가 아니라면 프리미티브 경계에 걸친 에지 픽셀이다. 이 판별 결과를 NormalEdge 버퍼의 Alpha에 기록하고, Coverage 값 자체도 별도 버퍼에 저장한다.

### 4.2 Edge Masking (MaskMSAAEdge — EdgeMaskingPS.hlsl)

G-Buffer 기록 후, 전체 화면을 대상으로 EdgeMaskingPS를 실행하여 Stencil Buffer에 에지를 마킹한다.

```hlsl
float4 main(psInput input) : SV_Target
{
    float sumEdge = 0;
    float invaildPos = 0;

    for (uint i = 0; i < SAMPLE_COUNT; ++i)
    {
        invaildPos += positionTex.Load(input.posProj.xy, i).w;
        uint edge = normalEdgeTex.Load(input.posProj.xy, i).w;
        sumEdge += (edge > 0) ? 1 : 0;
    }
    if (!sumEdge)        discard;  // coverage 에지가 없으면 → 에지 아님
    if (invaildPos == -1.0 * SAMPLE_COUNT)
        discard;  // 모든 샘플이 빈 공간이면 → 에지 아님

    // 추가 검증: 샘플 간 노멀/위치 차이 확인
    uint rough = 0;
    uint far = 0;
    float3 baseNormal = normalEdgeTex.Load(input.posProj.xy, 0).xyz;
    float3 basePosition = positionTex.Load(input.posProj.xy, 0).xyz;
    for (uint j = 1; j < SAMPLE_COUNT; ++j)
    {
        float angle = dot(baseNormal, normalEdgeTex.Load(input.posProj.xy, j).xyz);
        rough += (angle < 0.98) ? 1 : 0;

        float dist = length(basePosition - positionTex.Load(input.posProj.xy, j).xyz);
        far += (dist > 1.0) ? 1 : 0;
    }
    if (!rough && !far)  discard;  // 노멀도 위치도 비슷하면 → 에지 아님

    return float4(1, 0, 0, 0);
}
```

에지 판정은 3단계 필터로 구성된다:

1. **Coverage 기반 1차 필터** — G-Buffer에서 기록된 `edge` 플래그를 확인. 모든 샘플이 단일 프리미티브에 완전 덮여있으면(`sumEdge == 0`) discard
2. **유효성 검사** — 모든 샘플의 Position.w가 -1이면 빈 하늘이므로 discard
3. **기하학적 2차 필터** — Coverage 에지더라도 실제로 시각적 차이가 없을 수 있다. 샘플 간 **노멀 차이**(dot < 0.98)와 **위치 차이**(distance > 1.0)를 검사하여, 둘 다 유사하면 에지에서 제외한다

이 셰이더의 PSO는 `stencilMaskDSS`(StencilRef = 1)를 사용한다. discard되지 않은 픽셀은 Stencil 값이 1로 기록된다.

```
stencilMaskDSS:
  StencilFunc  = ALWAYS
  StencilPassOp = REPLACE  →  통과한 픽셀의 Stencil = StencilRef(1)
```

discard된 픽셀은 PS가 실행되지 않으므로 Stencil이 초기값(0)을 유지한다. 결과적으로 Stencil Buffer에 Edge(1) / NonEdge(0) 맵이 완성된다.

### 4.3 SSAO — Edge/NonEdge 분기 (SsaoPS.hlsl)

SSAO 패스에서는 동일한 Render Target에 대해 **두 번의 Draw Call**을 실행한다.

```cpp
// App.cpp — RenderSSAO()
Graphics::SetPipelineStates(Graphics::ssaoPSO);       // Stencil == 0 (NonEdge)
SimpleQuadRenderer::GetInstance()->Render();

Graphics::SetPipelineStates(Graphics::ssaoEdgePSO);    // Stencil == 1 (Edge)
SimpleQuadRenderer::GetInstance()->Render();
```

두 PSO의 차이는 Pixel Shader와 StencilRef뿐이다:

| PSO | Pixel Shader | StencilRef | DSS |
|---|---|---|---|
| `ssaoPSO` | `main()` | 0 | `stencilEqualDrawDSS` |
| `ssaoEdgePSO` | `mainMSAA()` | 1 | `stencilEqualDrawDSS` |

`stencilEqualDrawDSS`는 `StencilFunc = EQUAL`이므로, Stencil 값이 StencilRef와 같은 픽셀에서만 PS가 실행된다.

**main() — NonEdge (샘플 0번 1회)**

```hlsl
float main(psInput input) : SV_TARGET
{
    float3 worldNormal = normalEdgeTex.Load(input.posProj.xy, 0).xyz;
    float4 worldPos = positionTex.Load(input.posProj.xy, 0);
    ...
    float occlusionFactor = getOcclusionFactor(input.texcoord, viewPos.xyz, viewNormal);
    return 1.0 - (occlusionFactor * attenuation);
}
```

NonEdge에서는 4개 샘플이 모두 동일하므로, **샘플 0번 하나만** 읽어 AO를 1회 계산한다.

**mainMSAA() — Edge (유효 샘플별 가중 연산)**

```hlsl
float mainMSAA(psInput input) : SV_TARGET
{
    uint4 coverage;
    coverage.x = coverageTex.Load(input.posProj.xy, 0);
    ...
    uint4 sampleWeight = coverageAnalysis(coverage);

    float sumOcclusionFactor = 0.0;
    for (uint i = 0; i < SAMPLE_COUNT; ++i)
    {
        if (sampleWeightArray[i] == 0) continue;
        ...
        float occlusionFactor = getOcclusionFactor(...) * sampleWeightArray[i];
        sumOcclusionFactor += occlusionFactor * attenuation;
    }
    sumOcclusionFactor /= SAMPLE_COUNT;
    return 1.0 - sumOcclusionFactor;
}
```

Edge에서는 Coverage 버퍼를 읽어 `coverageAnalysis()`로 각 샘플의 가중치를 결정한다. 동일한 Coverage를 가진 샘플은 하나로 합쳐서 가중치를 높이고, Coverage가 0인(빈) 샘플은 건너뛴다. 이로써 중복 연산을 줄이면서도 정확한 멀티샘플 AO를 계산한다.

### 4.4 Shading — Edge/NonEdge 분기 (ShadingBasicPS.hlsl)

Shading 패스도 SSAO와 동일한 Stencil 분기 구조를 사용한다.

```cpp
// App.cpp — ShadingBasic()
Graphics::SetPipelineStates(Graphics::shadingBasicPSO);      // Stencil == 0
SimpleQuadRenderer::GetInstance()->Render();

Graphics::SetPipelineStates(Graphics::shadingBasicEdgePSO);   // Stencil == 1
SimpleQuadRenderer::GetInstance()->Render();
```

**main() — NonEdge (4샘플 평균 후 1회 라이팅)**

```hlsl
float4 main(psInput input) : SV_TARGET
{
    float3 normal = float3(0.0, 0.0, 0.0);
    normal += normalEdgeTex.Load(input.posProj.xy, 0).xyz;
    normal += normalEdgeTex.Load(input.posProj.xy, 1).xyz;
    normal += normalEdgeTex.Load(input.posProj.xy, 2).xyz;
    normal += normalEdgeTex.Load(input.posProj.xy, 3).xyz;
    normal /= SAMPLE_COUNT;
    // position, albedo, mer도 동일하게 4샘플 평균
    ...
    float3 ambientLighting = getAmbientLighting(ao, albedo, position, normal, metallic, roughness);
    float3 directLighting  = getDirectLighting(normal, position, albedo, metallic, roughness, true);
    return float4(ambientLighting + directLighting, 1.0);
}
```

NonEdge에서는 4개 샘플이 동일하다는 전제하에, 각 G-Buffer의 4샘플을 평균 내고 **라이팅은 1회만** 수행한다. 4샘플을 읽는 것은 `Texture2DMS`에서 단일 resolve보다 명시적 평균이 더 제어 가능하기 때문이다.

**mainMSAA() — Edge (샘플별 개별 라이팅)**

```hlsl
float4 mainMSAA(psInput input) : SV_TARGET
{
    float3 sumClampLighting = float3(0.0, 0.0, 0.0);
    uint vaildSampleCount = 0;

    for (uint i = 0; i < SAMPLE_COUNT; ++i)
    {
        float4 position = positionTex.Load(input.posProj.xy, i);
        if (position.w == -1.0) continue;  // 빈 샘플 스킵
        vaildSampleCount++;

        float3 normal = normalEdgeTex.Load(input.posProj.xy, i).xyz;
        float3 albedo = albedoTex.Load(input.posProj.xy, i).rgb;
        float3 mer    = merTex.Load(input.posProj.xy, i).rgb;
        ...
        float3 lighting = ambientLighting + directLighting;
        sumClampLighting += clamp(lighting, 0.0, 1000.0);
    }
    return float4(sumClampLighting / max(1e-3, vaildSampleCount), 1.0);
}
```

Edge에서는 각 샘플의 G-Buffer를 개별로 읽어 **샘플마다 독립적으로 라이팅을 계산**한 뒤 평균한다. `position.w == -1.0`인 빈 샘플은 건너뛰고, 유효 샘플 수로 나눠 정확한 결과를 얻는다. 이것이 진정한 MSAA 셰이딩이다.

### 4.5 Deferred → Forward 전환 (ConvertToMSAA)

Deferred 단계의 SSAO와 Shading은 Non-MSAA 렌더 타겟(`basicBuffer`, R16G16B16A16_FLOAT)에 기록된다. 이후 Forward Pass에서는 Skybox, Cloud, Water 등이 MSAA 렌더 타겟(`basicMSBuffer`)에 직접 그려져야 한다.

이 전환을 위해 `ConvertToMSAA()` 단계에서 Non-MSAA 결과를 MSAA 버퍼에 복사한다:

```cpp
void App::ConvertToMSAA()
{
    Graphics::context->OMSetRenderTargets(1, Graphics::basicMSRTV.GetAddressOf(), nullptr);
    Graphics::context->PSSetShaderResources(0, 1, Graphics::basicSRV.GetAddressOf());
    Graphics::SetPipelineStates(Graphics::samplingPSO);
    SimpleQuadRenderer::GetInstance()->Render();
}
```

Fullscreen Quad로 Non-MSAA 텍스처를 MSAA 버퍼에 샘플링한다. 이후 Forward 오브젝트들이 이 MSAA 버퍼 위에 직접 렌더링된다.

### 4.6 Forward Rendering Pass

Deferred 결과가 MSAA 버퍼로 전환된 후, Forward 오브젝트들이 그 위에 렌더링된다.

```cpp
// App.cpp — Render()
// 4. Forward Render Pass MSAA
if (m_camera.IsUnderWater()) {
    RenderFogFilter();
    RenderSkybox();
    RenderCloud();
    RenderWaterPlane();
} else {
    RenderMirrorWorld();
    RenderWaterPlane();
    RenderFogFilter();
    RenderSkybox();
    RenderCloud();
}
```

수중 여부에 따라 렌더링 순서가 달라진다:
- **지상**: Mirror(수면 반사) → WaterPlane → Fog → Skybox → Cloud
- **수중**: Fog → Skybox → Cloud → WaterPlane

Forward Pass의 결과는 `basicMSBuffer`(MSAA)에 누적되며, Post Effect 직전에 `ResolveSubresource()`로 최종 Non-MSAA 버퍼로 리졸브된다.

```cpp
Graphics::context->ResolveSubresource(
    Graphics::basicBuffer.Get(), 0,
    Graphics::basicMSBuffer.Get(), 0,
    DXGI_FORMAT_R16G16B16A16_FLOAT);
```

### 4.7 전체 파이프라인 요약

```
[0. Shadow Map]

[1. Deferred Pass]
  FillGBuffer()       5 MRT (4x MSAA) + SV_COVERAGE로 에지 플래그 기록
       ↓
  MaskMSAAEdge()      EdgeMaskingPS → Stencil Buffer에 Edge(1)/NonEdge(0) 마킹
       ↓                  (stencilMaskDSS, StencilRef=1)
  RenderSSAO()        ssaoPSO (Stencil==0) → main(): 샘플0 1회
                      ssaoEdgePSO (Stencil==1) → mainMSAA(): Coverage 가중 멀티샘플
       ↓
  ShadingBasic()      shadingBasicPSO (Stencil==0) → main(): 4샘플 평균 + 1회 라이팅
                      shadingBasicEdgePSO (Stencil==1) → mainMSAA(): 샘플별 개별 라이팅
       ↓
  → basicBuffer (Non-MSAA, R16G16B16A16_FLOAT)

[2. ConvertToMSAA]
  basicBuffer → basicMSBuffer (MSAA)로 복사

[3. Forward Pass — basicMSBuffer(MSAA)]
  Skybox, Cloud, Water, Fog 등 Forward 오브젝트 렌더링

[4. Post Effect]
  ResolveSubresource (MSAA → Non-MSAA)
  Bloom, Tone Mapping
```

## 5. 문제점 & 해결

### 5.1 Coverage 에지의 오탐(False Positive)

**문제**: `SV_COVERAGE != 0xf`인 픽셀이 반드시 시각적 에지는 아니다. 동일한 면 위에서 MSAA 그리드와 프리미티브 경계가 미세하게 걸치면 Coverage가 부분적이지만, 실제로는 같은 색상이어서 MSAA 처리가 불필요하다.

**해결**: EdgeMaskingPS에서 Coverage 1차 필터 후, 샘플 간 **노멀 차이**(dot < 0.98)와 **위치 차이**(> 1.0)를 추가로 검증하는 2차 필터를 적용했다. 두 조건 모두 유사하면 에지에서 제외하여 불필요한 MSAA 셰이딩을 줄인다.

### 5.2 Deferred와 Forward 간의 MSAA 버퍼 불일치

**문제**: Deferred 단계의 SSAO와 Shading은 Non-MSAA 버퍼에 결과를 쓰지만, Forward 오브젝트(Skybox, Cloud, Water)는 투명 블렌딩이 필요하여 MSAA 버퍼에 직접 렌더링해야 한다.

**해결**: `ConvertToMSAA()` 중간 단계를 삽입하여, Deferred 결과를 MSAA 버퍼로 복사한 뒤 Forward Pass를 진행한다. Forward 렌더링 완료 후 `ResolveSubresource()`로 최종 리졸브한다.

### 5.3 트레이드오프 — Stencil 2-Pass vs Compute Shader

Stencil 기반 2-Pass 방식은 추가 Draw Call이 필요하지만, 하드웨어 Stencil Test로 Early-Z 단계에서 불필요한 픽셀이 빠르게 제거되므로 오버헤드가 적다. Compute Shader로 분기를 처리하는 대안도 있지만, 기존 렌더 파이프라인과의 호환성과 구현 단순성 면에서 Stencil 방식을 선택했다.

## 6. 결과

- 전체 화면 4x MSAA 셰이딩 대비 에지 픽셀만 멀티샘플 처리하여 셰이딩 비용을 크게 절감한다
- Edge Masking의 3단계 필터(Coverage → 유효성 → 기하학적 차이)로 오탐을 최소화하여 불필요한 MSAA 연산을 억제한다
- Stencil Buffer 하나로 SSAO와 Shading 두 패스 모두에 동일한 Edge/NonEdge 분류를 재사용한다
- Deferred → Forward 전환이 ConvertToMSAA 한 단계로 처리되어 파이프라인이 단순하다

## 7. 회고

- Edge Masking의 임계값(노멀 각도 0.98, 위치 거리 1.0)이 하드코딩되어 있다. 씬에 따라 최적값이 다를 수 있으므로 조정 가능하게 하면 더 유연할 것이다
- SSAO의 `mainMSAA()`에서 `coverageAnalysis()`로 중복 샘플을 합치는 최적화가 적용되어 있지만, Shading의 `mainMSAA()`에서는 단순히 유효 샘플을 개별 처리한다. Shading에도 동일한 Coverage 가중 최적화를 적용하면 Edge 픽셀의 연산량을 더 줄일 수 있다
- 현재 SSAO 결과는 Non-MSAA 텍스처 하나에 기록되므로, Edge 픽셀의 SSAO도 단일 값이다. Shading의 mainMSAA에서 샘플별 AO를 개별 참조하지 못하는 한계가 있다
