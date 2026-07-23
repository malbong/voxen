# Deferred Shading with MSAA — Stencil 기반 Edge/NonEdge 분기 처리

<img width="1920" height="1080" alt="Image" src="https://github.com/user-attachments/assets/ce520e5f-921f-4f87-b5f2-bb899cd45f51" />

<img width="1920" height="1080" alt="Image" src="https://github.com/user-attachments/assets/2707fb57-c9ef-4775-b797-aedacaefdc9a" />

## 1. 개요

Deferred Shading 파이프라인에서 MSAA 안티에일리어싱을 효율적으로 처리하는 시스템이다.
전체 화면을 4x MSAA로 Shading 하는 대신, **Edge Masking** 단계에서 실제 에지인 픽셀만 Stencil로 마킹하고, 이후 SSAO와 Shading 패스를 Edge/NonEdge 두 번에 나누어 실행한다.
NonEdge 픽셀은 단일 샘플로 가볍게 처리하고, Edge 픽셀만 멀티 샘플로 처리하여 품질과 성능의 균형을 맞춘다.

## 2. 도입 동기

## 2.1 Deferred Shading을 넣은 이유

원래는 단순히 Forward Pass로만 이루어진 Voxel 렌더러였으나, SSAO를 직접 구현하고자 G-Buffer를 만들 필요가 있었고, 전체 파이프라인을 수정해야했다.
그래서 불투명 물체에 대해서는 Deferred Rendering을 진행하고, 반투명 물과 구름, 그리고 스카이박스는 Forward Rendering Pass 그대로 유지한다.

## 2.2 Edge/NonEdge 분기

이 때, Shading과 SSAO 연산은 MSAA으로 처리하기엔 부담이였고, Edge/NonEdge로 분기하여 체크하기로 결정했다.
G-Buffer에는 여러 MRT(Multi Render Target)가 4x MSAA로 기록되는데, Shading 단계에서 모든 픽셀의 4개 샘플을 각각 라이팅 연산하면 셰이딩 비용이 4배가 된다.
핵심 관찰은 **MSAA가 실제로 필요한 픽셀은 오브젝트 경계(Edge)에만 존재한다**는 것이다. Edge가 아닌 픽셀은 4개 샘플이 모두 같으므로 1번만 셰이딩하면 충분하다.
모든 픽셀에 Sample Count 만큼 연산하는 건 비효율적이기에 아래의 Nvidia 문서를 참고하여 로직을 작성했다.
Nvidia Archive https://archive.docs.nvidia.com/gameworks/content/gameworkslibrary/graphicssamples/d3d_samples/antialiaseddeferredrendering.htm

## 3. 핵심 아이디어

Stencil Buffer를 사용해 화면을 Edge(=1)와 NonEdge(=0) 영역으로 분류한 뒤, 동일한 렌더 타겟에 두 번의 Draw Call로 각 영역을 처리한다.

```
[G-Buffer Fill]  4x MSAA로 기하 정보 기록 (SV_COVERAGE로 MSAA 엣지 감지 준비)
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

| G-Buffer   | 포맷               | 내용                                                                             |
| ---------- | ------------------ | -------------------------------------------------------------------------------- |
| NormalEdge | R16G16B16A16_FLOAT | RGB: 월드 노멀, A: 에지 마스크 (−1: 빈 샘플, 0: Non-Edge, 1: Edge, 2: SemiAlpha) |
| Position   | R32G32B32A32_FLOAT | RGB: 월드 위치, A: 유효성 (-1이면 빈 픽셀)                                       |
| Albedo     | R16G16B16A16_FLOAT | RGB: 베이스 컬러                                                                 |
| Coverage   | R32_UINT           | SV_COVERAGE 비트마스크                                                           |
| MER        | R16G16B16A16_FLOAT | R: Metallic, G: Emission, B: Roughness                                           |

BasicPS.hlsl에서 `SV_COVERAGE`을 이용해 에지 여부를 판별한다:
cf. `SV_COVERAGE` 뿐만 아니라, `EdgeMaskingPS.hlsl`에서 Normal과 Position 등 여러 상황에 따른 추가 Edge 체크를 따로한다.

```hlsl
// BasicPS.hlsl
psOutput main(psInput input, uint coverage : SV_COVERAGE, uint sampleIndex : SV_SampleIndex)
{
    bool edge = (coverage != 0xf);  // 0xF가 아니면 → 일부 샘플만 덮음 → 에지

#ifdef USE_ALPHA_CLIP
    output.normalEdge = float4(normalize(normal), 2.0);         // SemiAlpha 마커
#else
    output.normalEdge = float4(normalize(normal), float(edge)); // 에지: 1.0, 비에지: 0.0
#endif
    output.coverage = coverage;
    ...
}
```

`SV_COVERAGE`는 4x MSAA에서 어떤 서브 샘플을 덮는지를 4비트 마스크로 나타낸다.
`0xf`(= 0b1111, 4개 모두 덮음)가 아니라면 프리미티브 경계에 걸친 에지 픽셀이다. Coverage 값 자체도 별도 버퍼에 저장한다.

NormalEdge 버퍼의 Alpha(`w`) 값은 4가지 값을 가진다:

| 마킹값 | 의미                                                   |
| ------ | ------------------------------------------------------ |
| −1.0   | 빈 샘플 (G-Buffer 클리어 값)                           |
| 0.0    | Non-Edge 샘플 (coverage == 0xf)                        |
| 1.0    | Edge 샘플 (coverage != 0xf)                            |
| 2.0    | `SemiAlpha` 샘플 (`USE_ALPHA_CLIP` 렌더링 — 잎, 풀 등) |

`SemiAlpha`(잎, 풀, 중간에 빈 텍스쳐 블록들)에 대한 마킹값을 따로 두었다.
그 이유는 다음과 같다.
<img width="732" height="594" alt="Image" src="https://github.com/user-attachments/assets/e64557a6-07e6-4651-903f-d7f22c5420dc" />

- 잎사귀 메쉬 자체는 하나의 픽셀에 모든 샘플이 맺혔으나, Texture Sampling 시에 Alpha 값을 연산하고 Alpha가 투명하면 `discard` 된다.
- 이 때, 같은 픽셀에 다른 샘플은 Alpha가 불투명하여 렌더링하게 되는데 `Coverage` 값이 0xF가 되어 Edge 판단을 하지 못하게 된다.
- 그래서 잎사귀와 같은 `SemiAlpha` 블록의 렌더링 시에는 다른 마킹값을 사용하여 추후에 `EdgeMaskingPS.hlsl`에서 추가 Edge 검사를 진행한다.

### 4.2 Edge Masking (MaskMSAAEdge — EdgeMaskingPS.hlsl)

G-Buffer 기록 후, 전체 화면을 대상으로 `EdgeMaskingPS.hlsl`를 실행하여 Stencil Buffer(`deferredDSV`)에 Edge를 마킹한다.

```hlsl
float4 main(psInput input) : SV_Target
{
    // 1. 마킹 값 체크
    const float INVALID_MASK = -1.0;
    const float EDGE_MASK = 1.0;
    const float SEMIALPHA_MASK = 2.0;

    uint invalidPosition = 0;
    uint edgeCount = 0;
    uint semiAlphaCount = 0;

    [unroll]
    for (uint i = 0; i < SAMPLE_COUNT; ++i)
    {
        float ne_w = normalEdgeTex.Load(input.posProj.xy, i).w; // invaild -1, noEdge 0, edge 1, semialpha 2

        if (ne_w == INVALID_MASK)
            ++invalidPosition;
        else if (ne_w == EDGE_MASK)
            ++edgeCount;
        else if (ne_w == SEMIALPHA_MASK)
            ++semiAlphaCount;
    }

    if (invalidPosition == SAMPLE_COUNT) // 유효하지 않은 위치가 SAMPLE 개수만큼 있으면 엣지가 아님
        discard;

    bool isSemiAlphaEdgePixel = (0 < semiAlphaCount && semiAlphaCount < SAMPLE_COUNT);
    if (edgeCount == 0 && !isSemiAlphaEdgePixel) // Edge 마킹이 없으며, SemiAlpha의 일부만 있는 게 아닌 픽셀은 엣지가 아님
        discard;

    // 2. rough, far 엣지 체크
    uint rough = 0;
    uint far = 0;
    float3 baseNormal = normalEdgeTex.Load(input.posProj.xy, 0).xyz;
    float3 basePosition = positionTex.Load(input.posProj.xy, 0).xyz;
    const float ROUGH_THRESHOLD = 0.98;
    const float DISTANCE_THRESHOLD = 1.0;

    [unroll]
    for (uint j = 1; j < SAMPLE_COUNT; ++j)
    {
        float angle = dot(baseNormal, normalEdgeTex.Load(input.posProj.xy, j).xyz);
        rough += (angle < ROUGH_THRESHOLD) ? 1 : 0;

        float dist = length(basePosition - positionTex.Load(input.posProj.xy, j).xyz);
        far += (dist > DISTANCE_THRESHOLD) ? 1 : 0;
    }

    if (!rough && !far) // 노멀 모두 비슷하고, 위치도 모두 비슷하면 엣지가 아님
        discard;

    return float4(1, 0, 0, 0);
}
```

에지 판정은 2단계 필터로 구성된다:

1. **마킹 값 기반 1차 필터** — `normalEdge.w` 값으로 각 샘플을 분류하고 두 가지 Edge 조건을 검사한다:
   - **유효성**: 모든 샘플이 −1.0(빈 샘플)이면 빈 공간이므로 discard
   - **Coverage값**: 기하적으로 Edge가 아니면서, SemiAlpha가 일부 샘플만 덮고있지 않은 경우 discard
   - 단순히 Coverage값만 이용해 Edge가 아닌 경우 discard해버리면, SemiAlpha가 Clip된 샘플의 경우에도 Coverage값이 0xF 라서 Edge 판단을 하지 못하는 경우가 있다. 그래서 SemiAlpha 마킹도 필요하다.
   - 아래는 SemiAlpha 검사를 하지 않는 경우와 하는 경우다.
   <div style="display: flex; gap: 10px;">
   <img width="713" height="671" alt="Image" src="https://github.com/user-attachments/assets/674952fb-8b81-45d6-91be-cc62ec5e928d" />
   <img width="713" height="671" alt="Image" src="https://github.com/user-attachments/assets/4edc69e6-85f3-485f-9b1d-e3c6df0d2a8e" />
   </div>

2. **기하학적 2차 필터** — 1차 필터를 통과한 픽셀에 대해 샘플 간 **노멀 차이**(dot < 0.98)와 **위치 차이**(distance > 1.0)를 추가 검증한다. 둘 다 유사하면 시각적으로 의미 없는 에지이므로 discard

이 셰이더의 PSO는 `stencilMaskDSS`(StencilRef = 1)를 사용한다. discard되지 않은 픽셀은 Stencil 값이 1로 기록된다.

```
stencilMaskDSS:
  StencilFunc  = ALWAYS
  StencilPassOp = REPLACE  →  통과한 픽셀의 Stencil = StencilRef(1)
```

discard된 픽셀은 PS가 실행되지 않으므로 Stencil이 초기값(0)을 유지한다. 결과적으로 Stencil Buffer에 Edge(1) / NonEdge(0) 맵이 완성된다.

### 4.3 SSAO & Lighting_PBR — Edge/NonEdge 분기

SSAO 패스에서는 동일한 Render Target에 대해 **두 번의 Draw Call**을 실행한다.

```cpp
// App.cpp — RenderSSAO()
Graphics::SetPipelineStates(Graphics::ssaoPSO);       // Stencil == 0 (NonEdge)
SimpleQuadRenderer::GetInstance()->Render();

Graphics::SetPipelineStates(Graphics::ssaoEdgePSO);    // Stencil == 1 (Edge)
SimpleQuadRenderer::GetInstance()->Render();
```

**main() — NonEdge (샘플 0번 1회)**

```hlsl
float main(psInput input) : SV_TARGET
{
    ... ssaoPS
}
float mainMSAA(psInput input) : SV_TARGET
{
    ... ssaoEdgePS
}
```

NonEdge에 대해서 한번 실행하고, Edge에 대해서 따로 실행한다.
Edge, NonEdge을 하나의 쉐이더 메인함수에서 `if`로 분기하여 사용할 수 있으나, Edge는 비용이 많이드는데 반해 Non-Edge는 비용이 적게 든다.
이러한 과정에서 먼저 끝낸 GPU 쓰레드가 대기 상태에 걸리게 되고, 한번에 실행 시키는 것이 비효율적라는 것을 문서를 통해 알게되었고, 따로 main 진입점을 분기하여 작성하였다.

<img width="862" height="235" alt="Image" src="https://github.com/user-attachments/assets/4d3b2dcb-9826-4353-9c6c-82b020630f89" />

분기 방식은 Lighting_PBR과 SSAO에 작성했다.

### 4.4 Deferred → Forward 전환 (ConvertToMSAA)

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
cf. MSAA -> Resolve는 지원하지만 Non-MSAA -> MSAA는 지원하지 않아 직접 샘플링한다.

### 4.5 전체 파이프라인 요약

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

[3. Forward Pass — basicMSBuffer(MSAA)] ...

[4. Post Effect] ...
```

## 5. 문제점 & 해결

### 5.1 Coverage Edge의 오탐(False Positive)

**문제**

- `Coverage`가 Edge라고 반드시 시각적인 Edge인 것은 아니다.

**해결**

- EdgeMaskingPS에서 마스크 1차 필터 후, 샘플 간 **노멀 차이**(dot < 0.98)와 **위치 차이**(> 1.0)를 추가로 검증하는 2차 필터를 적용했다. 두 조건 모두 유사하면 Edge에서 제외하여 불필요한 MSAA 셰이딩을 줄인다.

### 5.2 SemiAlpha 렌더링에서의 Edge 누락

**문제**

- 잎, 풀 같은 SemiAlpha 오브젝트는 alpha = 0인 픽셀을 `discard`로 처리하는데, discard된 샘플은 PS를 실행하지 않으므로 G-Buffer에 아무것도 기록되지 않는다.
- 추가로 SemiAlpha에서 Clip된 부분은 `Coverage`가 `0xF`로 Edge가 아닌 상황이 생긴다.
- 즉, Alpha Clip으로 인한 시각적인 Edge이지만 `Coverage` 값이 `0xF`가 되어 Edge 검사에 누락이 생기는 문제가 있었다.

**해결**

- BasicPS.hlsl에서 `USE_ALPHA_CLIP` 분기는 렌더된 샘플의 `normalEdge.w`를 2.0으로 마킹한다.
- EdgeMaskingPS는 이를 이용해 `semiAlphaCount`를 집계하고, 일부 샘플만 SemiAlpha로 덮인 픽셀(`0 < semiAlphaCount < SAMPLE_COUNT`)에 대해서는 이후 추가적인 Edge검사를 진행하면 된다.

```
bool isSemiAlphaEdgePixel = (0 < semiAlphaCount && semiAlphaCount < SAMPLE_COUNT);
if (edgeCount == 0 && !isSemiAlphaEdgePixel)
    discard;
```

### 5.3 메쉬 A, 메쉬 B가 모두 SemiAlpha 블록일 때, 같은 픽셀안에 상이 맺힌 경우에 대한 고찰

**문제**

- **문제 5.2**에서는 SemiAlpha가 부분적으로 존재하면 추가적인 Edge 검사를 진행했지만, 모든 샘플이 SemiAlpha로 덮혔을 때 고려했어야 했다.
- 메쉬 A, 메쉬 B가 모두 SemiAlpha 블록일 때, 같은 픽셀안에 상이 맺힌 경우 `edgeCount == 0`이고 `semiAlphaCount == SAMPLE_COUNT`라서 Edge 후보에서 자동으로 탈락한다.

**해결 : 트레이드오프**

- 픽셀 하나에 같은 SemiAlpha인 경우, 해당 픽셀을 Non-Edge 그대로 판단하기로 결정했다.
- 그 이유는, 프로젝트 특성상 SemiAlpha 블록이 매우 많고, 거기에 따른 불필요한 Edge 판단으로 비용에 문제가 있었기 때문이다.
<div style="display: flex; gap: 10px;">
   <img width="800" height="500" alt="Image" src="https://github.com/user-attachments/assets/be886ebb-7602-4134-b3b6-082e73e35787" />
   <img width="800" height="500" alt="Image" src="https://github.com/user-attachments/assets/aa5298af-332f-4ea9-b3e9-6ad05bc6d2f7" />
</div>

### 5.4 Deferred와 Forward 간의 MSAA 버퍼 불일치

**문제**

- Deferred 단계의 SSAO와 Shading은 Non-MSAA 버퍼에 결과를 쓰지만, Forward 오브젝트(Skybox, Cloud, Water)는 투명 블렌딩이 필요하여 MSAA 버퍼에 직접 렌더링해야 한다.

**해결**

- `ConvertToMSAA()` 중간 단계를 삽입하여, Deferred 결과를 MSAA 버퍼로 복사한 뒤 Forward Pass를 진행한다. Forward 렌더링 완료 후 `ResolveSubresource()`로 최종 리졸브한다.

## 6. NonEdge / Edge 분기 2-Pass로 얻은 결과

<img width="531" height="367" alt="Image" src="https://github.com/user-attachments/assets/afa8e6cc-5e13-4811-88be-4db60fd07de1" />

2-Pass 자체로 구분하여 하나의 파이프라인에서는 속도가 향상되었지만 Edge Masking 자체로 비용이 추가적으로 생기기에 프레임 전체에 대한 효과는 미비.

SSAO의 샘플이 늘어나거나, Lighting 연산이 복잡해질 때, Edge Masking 비용보다 높아지는 경우 이득일 것

## 7. 회고

- SSAO를 작성하기 위해 Full-Forward Rendering Pass를 Deferred Rendering으로 수정해야했고, 거기서 투명 물질에 대한 처리는 어떻게해야할까 고민이 정말 많았다.
  - 다른 AA 기법을 배우고 적용하는 시간이 클 것 같았고 그 결과에 대해 잘나오리라는 보장도 없기에 MSAA를 그대로 고집했다.
  - 그래서, MSAA를 그대로 적용하고 Deferred Rendering에서는 어떻게 하는게 올바른 방법인지 찾아보는게 어려웠다.
- Edge Testing과 분기, 그리고 Shading, SSAO 처리에 대해서 다양한 디버깅을 했어야해서 고려할게 많았다.
  - 단순히 Edge 체크를 못한 것인지, Edge에서 mainMSAA ShadingPS, SsaoPS가 잘못된 것인지 판단하기 힘들었다.
- Edge Masking의 임계값(Threshold: 노멀 각도 0.98, 위치 거리 1.0)이 하드코딩되어 있다. 적절한 것은 동적으로 바뀌면 될 것 같다.
- 주먹구구식으로 작성했지만 렌더링 결과에는 만족하고 Deferred Shading을 진행해볼 수 있는 기회가 되서 좋았다.
