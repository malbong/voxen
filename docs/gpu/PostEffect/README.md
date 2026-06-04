# Post Effect — HDR, Bloom, Tone Mapping, Filters

<table>
  <tr>
    <td align="center">
<img width="1915" height="1075" alt="Image" src="https://github.com/user-attachments/assets/bd617cec-a408-414c-8db6-3660d69e810c" />
<br/>
      <b>Linear</b>
    </td>
    <td align="center">
<img width="1915" height="1076" alt="Image" src="https://github.com/user-attachments/assets/3d1b9827-bf40-48c5-bf1f-8f3715dc8948" />
<br/>
      <b>Reinhard</b>
    </td>
  </tr>
  <tr>
    <td align="center">
<img width="1916" height="1072" alt="Image" src="https://github.com/user-attachments/assets/3b433149-bcee-42fe-93d1-170429398b58" />
<br/>
      <b>Luma Based Reinhard</b>
    </td>
    <td align="center">
<img width="1913" height="1073" alt="Image" src="https://github.com/user-attachments/assets/820b4269-6d8d-4dff-a0df-783a26d43a01" />
<br/>
      <b>White Preserving Reinhard</b>
    </td>
  </tr>
  <tr>
    <td align="center">
<img width="1914" height="1073" alt="Image" src="https://github.com/user-attachments/assets/9480d2e3-852a-42d6-a1fe-09d7137e4075" />
<br/>
      <b>Luma Based White Preserving Reinhard</b>
    </td>
    <td align="center">
      
<img width="1911" height="1070" alt="Image" src="https://github.com/user-attachments/assets/9ab8a2dc-e59c-4981-9d34-93ed794db45b" />
<br/>
      <b>Filmic</b>
    </td>
  </tr>
  <tr>
    <td align="center">
      
<img width="1916" height="1073" alt="Image" src="https://github.com/user-attachments/assets/e2af80fb-5f71-41f4-b2b1-e7adfca64407" />
<br/>
      <b>Uncharted</b>
    </td>
    <td></td>
  </tr>
</table>

<table>
  <tr>
    <td align="center">
<img width="1913" height="1075" alt="Image" src="https://github.com/user-attachments/assets/14e44c5e-5dfe-4aa9-8140-2ea861430971" />
<br/>
      <b>Linear</b>
    </td>
    <td align="center">
<img width="1916" height="1072" alt="Image" src="https://github.com/user-attachments/assets/9ffb39c2-d2cc-4813-9310-8cf958ee3799" />
<br/>
      <b>Reinhard</b>
    </td>
  </tr>
  <tr>
    <td align="center">
<img width="1912" height="1073" alt="Image" src="https://github.com/user-attachments/assets/264bcc5b-d40f-4f83-b897-b74ec90466af" />
<br/>
      <b>Luma Based Reinhard</b>
    </td>
    <td align="center">
<img width="1916" height="1075" alt="Image" src="https://github.com/user-attachments/assets/b049735c-366d-4594-a06c-d0c4b4dd958b" />
<br/>
      <b>White Preserving Reinhard</b>
    </td>
  </tr>
  <tr>
    <td align="center">
<img width="1917" height="1076" alt="Image" src="https://github.com/user-attachments/assets/b5be4005-d02c-4b03-844c-a7b212738904" />
<br/>
      <b>Luma Based White Preserving Reinhard</b>
    </td>
    <td align="center">
<img width="1913" height="1072" alt="Image" src="https://github.com/user-attachments/assets/02de1b78-eda1-4209-8e3d-21e1cce74899" />
<br/>
      <b>Filmic</b>
    </td>
  </tr>
  <tr>
    <td align="center">
<img width="1913" height="1073" alt="Image" src="https://github.com/user-attachments/assets/15ce3f68-81f6-4d35-bb93-33c687c0ef99" />
<br/>
      <b>Uncharted</b>
    </td>
    <td></td>
  </tr>
</table>

## 1. 개요

Voxen의 후처리(Post Effect)는 Deferred / Forward 셰이딩이 끝난 **HDR(R16G16B16A16_FLOAT) 이미지**를 입력 받아, 최종 LDR 백버퍼로 변환하기까지의 모든 화면 단위 효과를 처리한다.

대상으로는 다음과 같이 구성된다.

1. **Fog Filter** — 거리 기반 안개. `Texture2DMS` 접근이 필요하여 **Forward Pass 중간**에 배치
2. **Water Filter** — 수중 진입 시 파란 틴트(tint). 일반 알파 블렌딩 단일 패스
3. **Bloom (Down/Up)** — 4단계 다운샘플 + 4단계 업샘플 피라미드
4. **Combine + Tone Mapping + Gamma Correction** — HDR을 LDR sRGB로 변환

핵심 아이디어:

- HDR 라이팅 결과를 보존하기 위해 모든 중간 버퍼는 `R16G16B16A16_FLOAT`
- Bloom의 Threshold는 **사용하지 않음** — HDR 값 자체가 임계 역할을 한다 (5.1)
- Tone Mapping은 7종을 등록해두고 런타임 스위치로 비교 가능, 기본은 `WhitePreservingLumaBasedReinhard`
- 합성 단계의 Bloom 강도는 **Henyey-Greenstein Phase Function**으로 태양 방향에서 강화된다

## 2. 도입 동기

### 2.1 HDR이 필요한 이유

라이팅 결과는 `radianceWeight`(최대 2.0)와 specular BRDF의 곱이 자유롭게 1.0을 초과한다.
렌더타겟이 `[0, 1]` LDR이라면 1.0 초과분이 그대로 잘려나가, 태양 하이라이트와 발광 블록의 밝기 차이가 사라지고 **모든 밝은 영역이 순백색으로 뭉개진다**.

`R16G16B16A16_FLOAT`은 이론적으로 65504까지 저장할 수 있고, 후처리 모든 단계에서 이 정밀도를 유지해야 Bloom·Tone Mapping이 의도대로 동작한다.

### 2.2 Bloom이 필요한 이유

HDR만 가지고 있어선 “숫자상” 밝을 뿐이고, 화면상으로는 결국 `[0,1]`로 압축되어 표시된다.
실제 카메라에서 강한 빛은 렌즈 산란으로 주변까지 번지는데, 이를 모사하려면 HDR의 밝은 영역을 **블러해 주변에 더해줘야** 한다.

### 2.3 Tone Mapping & Gamma Correction이 필요한 이유

- 모니터는 결국 `[0,1]`만 표현 가능 → HDR 값을 자연스럽게 압축할 곡선이 필요
- 라이팅을 Linear 공간에서 했으므로, 최종 출력은 sRGB로 보정해야 모니터가 의도한 밝기를 보여줌

### 2.4 Fog / Water Filter

- 거리에 따라 색이 안개에 흡수되는 효과는 깊이를 알아야 가능
- 수중 진입은 색역(파란빛 + 흐림) 자체를 바꿔야 하므로, Forward 셰이딩과는 분리된 단일 패스로 처리

## 3. 핵심 아이디어

### 3.1 후처리 단계 배치 (App::Render)

```cpp
// 4. Forward Render Pass MSAA
if (m_camera.IsUnderWater()) {
    RenderFogFilter();      // ← Forward 중간
    RenderSkybox();
    RenderCloud();
    RenderWaterPlane();
}
else {
    RenderMirrorWorld();
    RenderWaterPlane();
    RenderFogFilter();      // ← Forward 중간
    RenderSkybox();
    RenderCloud();
}

// 5. Post Effect
Graphics::context->ResolveSubresource(basicBuffer, 0, basicMSBuffer, 0, R16G16B16A16_FLOAT);
if (m_camera.IsUnderWater())
    RenderWaterFilter();    // Resolve된 단일샘플 버퍼 위에서 동작
RenderBloom();              // Bloom + Combine + ToneMapping
```

FogFilter만 Forward 패스 안쪽으로 끌려 들어가는 이유는 `Texture2DMS<float4, SAMPLE_COUNT>`로 **MSAA 샘플을 직접 읽어야 하기 때문**이다 (5.2).

### 3.2 Bloom 피라미드

```
Down (해상도 줄이기)               Up (해상도 복원)
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
[원본 basic]  ──→  [1/2]  ──→  [1/4]  ──→  [1/8]  ──→  [1/16]
                                                          │
[결과 bloom]  ←──  [1/2]  ←──  [1/4]  ←──  [1/8]  ←──────┘
```

- 다운: 해상도가 절반씩 줄면서, 저해상도 1텍셀의 블러가 원본 기준의 **넓은 블러**가 된다
- 업: 다시 두 배로 복원하면서 가중평균으로 부드럽게 확장
- 결과적으로 **좁은 글로우(고해상도)부터 넓은 글로우(저해상도)까지 동시 누적**된다

### 3.3 Tone Mapping 컬렉션

`CombineBloomPS.hlsl`에는 7종이 등록되어 있고, `toneMappingFunctionIndex` 상수로 런타임 전환한다.

| Index | Name                             | 특징 (직관)                                                            |
| ----- | -------------------------------- | ---------------------------------------------------------------------- |
| 1     | Linear                           | 단순 Clamp. 1.0 초과 영역 정보가 모두 소실됨                           |
| 2     | Reinhard                         | `c/(1+c)`로 무조건 압축. 어둡고 흐리멍덩한 톤이 되기 쉬움              |
| 3     | LumaBasedReinhard                | 휘도(luma)에만 압축 적용 → 색감(채도) 유지                             |
| 4     | WhitePreservingReinhard          | "흰색 기준점" `w`를 두어 그 이하는 압축, 그 이상은 1로 고정            |
| 5     | WhitePreservingLumaBasedReinhard | 4 + 3 결합. **현재 기본값**                                            |
| 6     | Filmic                           | 영화 카메라풍의 S-Curve. 어두운 부분은 들어올리고 밝은 부분은 부드럽게 |
| 7     | Uncharted2                       | Naughty Dog Hable 함수. S-Curve + White Point 정규화                   |

### 3.4 Henyey-Greenstein 기반 Bloom 강도

대기 산란에서 빛이 한 방향으로 얼마나 치우쳐 산란되는지를 묘사하는 함수.
카메라가 태양을 정면으로 볼수록 강하게, 등질수록 약하게 → Bloom 강도(`bloomStrength`)로 변환하여 **태양을 볼 때 화면이 더 부풀어 보이는** 효과를 만든다.

## 4. 구현 내용

### 4.1 HDR 버퍼 구성 (Graphics.cpp)

```cpp
DXGI_FORMAT format = DXGI_FORMAT_R16G16B16A16_FLOAT;

// basicMSBuffer: Deferred/Forward의 MSAA 렌더 타겟
// basicBuffer:   Resolve된 단일샘플 HDR 결과
// bloomBuffer[0..4]: 피라미드. 한 단계마다 가로/세로 1/2
UINT bloomWidth = APP_WIDTH, bloomHeight = APP_HEIGHT;
for (int i = 0; i < 5; ++i) {
    DXUtils::CreateTextureBuffer(bloomBuffer[i], bloomWidth, bloomHeight, false, format, bindFlag);
    bloomWidth /= 2;
    bloomHeight /= 2;
}
```

| 버퍼                      | 해상도      | 포맷               | 용도                      |
| ------------------------- | ----------- | ------------------ | ------------------------- |
| `basicMSBuffer`           | 원본 (MSAA) | R16G16B16A16_FLOAT | Forward까지의 셰이딩 결과 |
| `basicBuffer`             | 원본        | R16G16B16A16_FLOAT | MSAA Resolve된 HDR        |
| `bloomBuffer[0]`          | 원본        | R16G16B16A16_FLOAT | Bloom 최종 (업샘플 결과)  |
| `bloomBuffer[1..4]`       | 1/2 ~ 1/16  | R16G16B16A16_FLOAT | 피라미드 중간 단계        |
| `copyForwardRenderBuffer` | 원본 (MSAA) | R16G16B16A16_FLOAT | FogFilter용 입력 복사본   |
| `backBuffer`              | 원본        | Swap Chain LDR     | 최종 출력                 |

### 4.2 Fog Filter (FogFilterPS.hlsl)

Beer–Lambert 법칙 기반의 거리 안개. **MSAA 샘플 단위**로 동작한다.

```hlsl
Texture2DMS<float4, SAMPLE_COUNT> renderTex : register(t0);
Texture2DMS<float,  SAMPLE_COUNT> depthTex  : register(t1);

float getFogFactor(float3 pos)
{
    float dist = length(pos.xyz);
    float distFactor = saturate((dist - fogDistMin) / (fogDistMax - fogDistMin));
    float beerLambert = exp(-fogStrength * distFactor);
    return 1.0 - beerLambert;
}

float4 main(psInput input, uint sampleIndex : SV_SampleIndex) : SV_TARGET
{
    float3 fogColor    = getFogColor(lightDir, eyeDir);
    float3 renderColor = renderTex.Load(input.posProj.xy, sampleIndex).rgb;

    float depth   = depthTex.Load(input.posProj.xy, sampleIndex).r;
    float3 viewPos = texcoordToViewPos(input.texcoord, depth);

    float fogFactor  = getFogFactor(viewPos);
    float3 blendColor = lerp(renderColor, fogColor, fogFactor);
    return float4(blendColor, 1.0);
}
```

호출 측에서는 RTV 자기 참조를 피하기 위해 `CopyResource`로 **현재 MSAA 결과의 사본**을 만들고, 그 사본을 SRV로 바인딩한 뒤 원본 MSAA RTV로 다시 그린다.

```cpp
void App::RenderFogFilter()
{
    Graphics::context->CopyResource(Graphics::copyForwardRenderBuffer.Get(),
                                    Graphics::basicMSBuffer.Get());
    Graphics::context->OMSetRenderTargets(1, Graphics::basicMSRTV.GetAddressOf(), nullptr);
    m_postEffect.FogFilter();
}
```

수중인지 여부에 따라 `fogDistMin/Max/Strength`가 매 프레임 갱신되며, 수중에서는 적응 시간(`m_waterAdaptationTime`)에 따라 점차 더 짙고 가까이서 안개가 끼도록 보간된다.

### 4.3 Water Filter (WaterFilterPS.hlsl)

수중 진입 시 화면 전체에 파란 틴트를 알파 블렌딩으로 얹는 가장 단순한 패스다.

```hlsl
float4 main(psInput input) : SV_TARGET
{
    float clampedRadianceWeight = clamp(radianceWeight, 0.1, 1.0);
    float blendAlpha = filterStrength;
    return float4(filterColor * clampedRadianceWeight, blendAlpha);
}
```

- `filterColor`는 sRGB → Linear 변환된 파란 계열 색
- `radianceWeight`로 곱해 **밤에는 자동으로 어두워지도록** 한다 (한낮의 푸른 빛 ≠ 한밤의 푸른 빛)
- 블렌딩은 `alphaBS` PSO → `lerp(dst, src, src.a)`

PostEffect 단계의 Resolve가 끝난 `basicBuffer` 위에 그대로 alpha-blend 한다.

### 4.4 Bloom Down (BloomDownPS.hlsl)

Call of Duty의 발표로 알려진 13-tap 가중 다운샘플.

```hlsl
// a - b - c    j k l m / 4 * 0.5
// - j - k -    a b d e / 4 * 0.125
// d - e - f    b c e f / 4 * 0.125
// - l - m -    d e g h / 4 * 0.125
// g - h - i    e f h i / 4 * 0.125

color += (j + k + l + m) * 0.25 * 0.5;     // 안쪽 4점
color += (a + b + d + e) * 0.25 * 0.125;   // 모서리 4개 2×2 블록
color += (b + c + e + f) * 0.25 * 0.125;
color += (d + e + g + h) * 0.25 * 0.125;
color += (e + f + h + i) * 0.25 * 0.125;
```

- 안쪽 사각형 4점은 가장 큰 비중(`0.5/4`), 모서리 블록들은 각 `0.125/4`
- 중심 `e`는 4개의 모서리 블록 모두에 포함 → **텐트(tent) 분포**가 형성된다
- 단순 박스 필터보다 **Firefly(밝은 점이 화면 가득 번지는 아티팩트) 억제**에 강하다

#### 4단계 다운샘플 체인

```cpp
// PostEffect::Bloom — Down
for (int i = 0; i <= count; ++i) {   // count = 3
    int div = (int)pow(2, i + 1);
    UpdateViewport(bloomViewport, 0, 0, APP_WIDTH/div, APP_HEIGHT/div);
    OMSetRenderTargets(1, bloomRTV[i + 1], nullptr);
    PSSetShaderResources(0, 1, i == 0 ? basicSRV : bloomSRV[i]);
}
```

| 패스   | 입력                  | 출력          | 출력 해상도 |
| ------ | --------------------- | ------------- | ----------- |
| Down 0 | `basicSRV` (원본 HDR) | `bloomRTV[1]` | 1/2         |
| Down 1 | `bloomSRV[1]`         | `bloomRTV[2]` | 1/4         |
| Down 2 | `bloomSRV[2]`         | `bloomRTV[3]` | 1/8         |
| Down 3 | `bloomSRV[3]`         | `bloomRTV[4]` | 1/16        |

첫 패스의 입력이 **원본 HDR 그대로**라는 점이 중요하다. 별도의 Brightness Threshold 차감/마스킹 없이 진행한다 (5.1).

### 4.5 Bloom Up (BloomUpPS.hlsl)

3×3 텐트(1-2-1) 가중 평균.

```hlsl
// a - b - c   →  1  2  1
// d - e - f   →  2  4  2   → /16
// g - h - i   →  1  2  1

color += (a + c + g + i);          // 모서리 ×1
color += (b + d + f + h) * 2.0;    // 변 ×2
color += e * 4.0;                  // 중심 ×4
color /= 16.0;
```

`linearClampSS` 샘플러로 읽기 때문에 하드웨어 바이리니어가 한 번 더 들어가 **소프트웨어 텐트 + 하드웨어 바이리니어**의 이중 평활화가 적용된다. 업스케일에서의 블록 아티팩트를 깔끔하게 막아준다.

| 패스 | 입력                 | 출력          | 출력 해상도 |
| ---- | -------------------- | ------------- | ----------- |
| Up 0 | `bloomSRV[4]` (1/16) | `bloomRTV[3]` | 1/8         |
| Up 1 | `bloomSRV[3]` (1/8)  | `bloomRTV[2]` | 1/4         |
| Up 2 | `bloomSRV[2]` (1/4)  | `bloomRTV[1]` | 1/2         |
| Up 3 | `bloomSRV[1]` (1/2)  | `bloomRTV[0]` | 원본        |

### 4.6 Combine + Tone Mapping (CombineBloomPS.hlsl)

#### 4.6.1 Henyey-Greenstein Phase

```hlsl
float henyeyGreensteinPhase(float3 L, float3 V, float aniso)
{
    float cosT = dot(L, V);
    float g = aniso;
    return (1.0 - g * g) / (4.0 * PI * pow(abs(1.0 + g * g - 2.0 * g * cosT), 1.5));
}
```

- 본래 의미: 산란 매질(구름·안개·물·대기)에서 한 광자가 입사 방향 대비 어느 방향으로 산란될 확률을 나타내는 phase function
- `g`(anisotropy): `+1`에 가까우면 **전방 산란**, `-1`에 가까우면 후방 산란, `0`이면 등방
- 여기선 `L = lightDir`(태양 → 점), `V = eyeDir`(눈 → 점)을 그대로 넣어 둘이 정렬될수록 큰 값을 얻는다
- `g = 0.9`로 강한 전방 산란을 설정 → **태양을 정면으로 볼 때 폭발적으로 커짐**

사용처는 Bloom 합성 가중치:

```hlsl
float scattering   = min(henyeyGreensteinPhase(lightDir, eyeDir, 0.9), 1.0) * 0.3;
float bloomStrength = scattering + (isUnderWater ? 0.125 : 0);
bloomStrength *= (useBloom ? 1.0 : 0.0);

float3 combineColor = lerp(renderColor, bloomColor, bloomStrength);
```

즉, **태양 방향에서의 빛 번짐 + 수중에서 살짝 더 흐릿한 분위기**가 조합된다.

#### 4.6.2 Exposure

```hlsl
float exposure = 1.5;
combineColor *= exposure;
```

전체 HDR 값을 한 번 1.5배로 올려 더 밝게 만든 뒤, 톤매핑 곡선이 다시 `[0,1]`로 압축한다.
"노출이 너무 짧다 / 길다" 같은 카메라 노출의 직관과 같은 역할이며, 자동 노출(Auto Exposure)은 적용되어 있지 않다.

#### 4.6.3 Tone Mapping 7종 — 직관적 해설

##### (1) Linear — 단순 Clamp

```hlsl
color = clamp(color, 0.0, 1.0);
```

1보다 큰 값은 무조건 1로 잘림. 강한 햇빛, 정반사, 발광 블록의 미묘한 밝기 차이가 **모두 순백색으로 똑같이 보이게** 되어 정밀도를 잃는다.

##### (2) Simple Reinhard — 부드럽게 압축, 그러나 흐릿

```hlsl
color = color / (1.0 + color);
```

- `c = 0` → `0`, `c = 1` → `0.5`, `c = ∞` → `1`
- 어떤 값이 들어와도 부드럽게 `[0,1]`로 압축된다
- 단점: `c = 1`이 이미 `0.5`로 깎이기 때문에 **전반적으로 어둡고 채도가 낮은 흐릿한 톤**이 된다

##### (3) Luma-Based Reinhard — 색감을 보존하면서 밝기만 압축

```hlsl
float luma          = dot(color, float3(0.2126, 0.7152, 0.0722));
float toneMappedLuma = luma / (1.0 + luma);
color *= toneMappedLuma / luma;
```

- 가중치 `(0.2126, 0.7152, 0.0722)`는 **Rec.709(sRGB) 표준 휘도 계수** — 사람의 눈이 G에 가장 민감, B에 가장 둔감하다는 사실을 반영
- 채널마다 따로 압축하지 않고 **luma만 압축한 뒤 비율을 그대로 색에 적용** → 색상(Hue/Saturation)은 거의 안 변한다
- 효과: Simple Reinhard 대비 **채도가 살아 있는 압축**

##### (4) White-Preserving Reinhard — 밝은 영역 살리기

```hlsl
float white  = 2.25;
float white2 = white * white;
color = color * (1.0 + color / white2) / (1.0 + color);
```

식을 풀어 의미를 보면:

- `c = w` (= 2.25) → 분자 `c · (1 + 1/w) = c + c/w`, 분모 `1 + c`
  - 계산 시 결과는 **정확히 1.0** — `w` 값이 “흰색 기준점”이 된다
- `c < w` → 분자가 분모보다 살짝 큰 양의 보정만 들어가, Simple Reinhard보다 **덜 어둡게** 압축됨
- `c > w` → 결과가 1을 살짝 넘기지만 이후 클램프/감마에서 자연스레 1로 수렴
  - 즉 “**`w`를 넘는 밝기는 흰색으로 살린다**”

직관적으로: 그냥 Reinhard가 "모든 밝기를 0~1로 우그러뜨려" 흐릿하다면, White-Preserving은 "**`w`까지가 1**" 이라는 기준을 둬서 **밝은 부분이 시원하게 살아남는다**.

##### (5) White-Preserving + Luma-Based Reinhard — **현재 기본값**

```hlsl
float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
float toneMappedLuma = luma * (1.0 + luma / white2) / (1.0 + luma);
color *= toneMappedLuma / luma;
```

(4)와 (3)의 결합: **흰 기준점을 지키면서, 채도까지 보존**한다. 가장 깨끗한 결과를 내기 때문에 기본값.

##### (6) Filmic (Hable / Jim Hejl)

```hlsl
color = (color * (6.2 * color + 0.5)) / (color * (6.2 * color + 1.7) + 0.06);
```

- 영화 필름의 응답 곡선을 흉내낸 S-Curve
- 어두운 부분은 약간 들어올리고 밝은 부분은 자연스럽게 둥글게 압축
- 이 함수는 출력 자체에 감마(1/2.2)가 내포되어 있어 별도 감마 보정을 거치지 않는다

##### (7) Uncharted 2 (Hable Filmic)

```hlsl
float A=0.15, B=0.50, C=0.10, D=0.20, E=0.02, F=0.30, W=11.2;
color = ((c*(A*c + C*B) + D*E) / (c*(A*c + B) + D*F)) - E/F;
float white = same_function(W);
color /= white;
```

Uncharted 2가 사용한 톤매핑. 7개의 상수(A~F, W)가 곡선의 어깨/발의 모양을 결정하며, **흰색 기준점 W = 11.2 HDR 값**까지 살린 뒤 `/white`로 정규화한다. 자유도가 높지만 튜닝이 어렵다.

#### 4.6.4 Gamma Correction

```hlsl
float3 gammaCorrection(float3 color, float gamma)
{
    float3 invGamma = float3(1, 1, 1) / gamma;
    return pow(color, invGamma);
}
```

- Linear 공간에서 라이팅을 마친 색을 **sRGB(모니터)** 공간으로 변환
- `pow(c, 1/2.2)`는 어두운 영역을 들어올리고 밝은 영역을 살짝 누르는 곡선
  - 정확히는 모니터의 `pow(x, 2.2)` 응답을 상쇄
- Filmic을 제외한 모든 톤매핑이 마지막 단계로 이 보정을 호출

#### 4.6.5 합성 흐름

```hlsl
// 1) Bloom 합성
float scattering    = min(henyeyGreensteinPhase(lightDir, eyeDir, 0.9), 1.0) * 0.3;
float bloomStrength = scattering + (isUnderWater ? 0.125 : 0);
bloomStrength      *= (useBloom ? 1.0 : 0.0);
float3 combineColor = lerp(renderColor, bloomColor, bloomStrength);

// 2) Exposure
combineColor *= 1.5;

// 3) Tone Mapping (switch toneMappingFunctionIndex)
//    default = whitePreservingLumaBasedReinhard

return float4(combineColor, 1.0);
```

### 4.7 전체 파이프라인 요약

```
[Deferred + Forward Shading (MSAA)]
basicMSBuffer ─┐
               ├──→ FogFilter (Texture2DMS, 샘플 단위)
               │      └─ CopyResource → copyForwardRenderBuffer 를 입력으로 자기 참조 회피
               ▼
[Skybox / Cloud / WaterPlane] 그려서 basicMSBuffer 완성
               │
               ▼
[ResolveSubresource]  basicMSBuffer → basicBuffer
               │
               ▼  (수중일 때만)
[WaterFilter]  basicBuffer 위에 알파 블렌딩
               │
               ▼
[Bloom Down] basicSRV → bloomRTV[1..4]  (1/2 .. 1/16)
               │
[Bloom Up]   bloomSRV[4..1] → bloomRTV[3..0]   (1/8 .. 원본)
               │
[Combine + ToneMapping]
   basicSRV (원본 HDR) + bloomSRV[0] (Bloom)
   → HG로 bloomStrength 결정 → lerp
   → exposure × 1.5
   → toneMapping(toneMappingFunctionIndex)
   → gammaCorrection(1/2.2)
               │
               ▼
       backBufferRTV (LDR)
```

## 5. 문제점 & 해결

### 5.1 Bloom Threshold를 두지 않은 이유 (HDR과의 관계)

**문제 의식**: 전통적으로 Bloom은 "밝기 임계값 이상만 분리해서 블러" 하는 방식이 표준처럼 알려져 있다. Voxen에서는 Threshold를 두지 **않는다**.

**이유**:

- 입력이 이미 HDR(`R16G16B16A16_FLOAT`)이라 1.0을 초과하는 정보가 살아있다. 다운샘플 가중 평균을 거치면 **어두운 영역의 값은 가중평균으로 옅어지고, 매우 밝은 영역은 그 큰 절대값 그대로 살아남는다**.
- 따라서 별도의 `if (luma > threshold) ...` 마스킹 없이도, **밝기 차이만으로 자연 분리**된다.
- 합성 시 `lerp(render, bloom, bloomStrength)`에서 `bloomStrength`가 작으면(0.0~0.3) Bloom 기여가 작아, 어두운 영역에 더해지는 미세한 번짐도 시각적으로 거의 보이지 않는다.

**부작용 / 절충**: 어두운 영역에도 미세하게 블러가 더해진다. 강한 선택적 글로우가 필요한 경우엔 Threshold가 유리하지만, Voxen의 자연 풍경 톤에서는 **부드러운 전체 글로우 쪽이 더 자연스럽다**.

### 5.2 FogFilter가 PostEffect가 아닌 Forward 패스에 있는 이유

**문제**: FogFilter는 “depth를 보고 거리를 계산해 색을 섞는” 전형적인 후처리지만, MSAA를 쓰는 한 Resolve된 결과 위에서 적용하면 안 된다.

**원인**:

- Forward 결과는 `basicMSBuffer`로 **MSAA 텍스처**, 깊이 또한 MSAA 깊이 버퍼
- Resolve 후의 단일샘플 컬러에 안개를 입히면, **샘플마다 다른 깊이가 평균된 결과**에 대해 안개를 계산하게 되어 픽셀 가장자리의 안개 강도가 부정확해진다

**해결**: `Texture2DMS<float4, SAMPLE_COUNT>`로 MSAA 컬러/깊이를 **샘플 단위로 직접 읽고**, `SV_SampleIndex` PS로 샘플별로 안개를 적용한다.
이를 위해 PostEffect 단계가 아닌 Forward 패스 중간에 호출되어야 한다.

```cpp
// App::Render — Forward 중간
RenderFogFilter();   // ← Texture2DMS 접근. Resolve 전이어야 함
RenderSkybox();
RenderCloud();
```

또한 RTV 자기참조(자기 자신을 읽으면서 쓰기)를 피하기 위해 `CopyResource(copyForwardRenderBuffer, basicMSBuffer)`로 사본을 떠서 입력으로 쓴다.

### 5.3 Bloom Down에서 Firefly 아티팩트

**문제**: HDR 입력에서 1~2픽셀짜리 극단적 하이라이트(예: specular)가 다운샘플마다 주위를 지배해 **한 점이 한 줄의 빛으로 번지는** 현상.

**해결**: 13-tap 가중 텐트 필터.

- 중앙 4점(`j,k,l,m`)에 큰 가중치(`0.5/4` 씩), 외곽 사각형 4개에 작은 가중치(`0.125/4` 씩)
- 중심 `e`가 외곽 사각형 모두에 포함되어 누적 가중이 가장 크다 → **중심으로 응축**
- 단순 박스 필터보다 **outlier 영향이 작다**

### 5.4 Bloom Up에서의 계단 아티팩트

**문제**: 저해상도 결과를 단순 바이리니어로 확대하면 1/16 해상도의 텍셀 격자가 무늬처럼 보일 수 있다.

**해결**: 3×3 `[1,2,4,2,1]` 텐트 필터 + `linearClampSS`의 하드웨어 바이리니어. 두 단계의 평활화가 겹치면서 부드러운 글로우가 만들어진다.

### 5.5 Tone Mapping 선택에 따른 톤/채도 변화

**문제**:

- Simple Reinhard는 모든 값을 우그러뜨려 **전반적으로 어둡고 흐리멍덩**
- Linear는 1.0 초과 영역의 정보가 모두 죽어 **하이라이트가 다 흰색**

**해결 / 절충**:

- 기본값으로 **White-Preserving + Luma-Based Reinhard**를 선택
  - 흰색 기준점 `w = 2.25`로 밝은 영역을 살리고
  - luma만 압축하여 채도 손실을 줄임
- 사용자가 비교해볼 수 있도록 7종을 모두 등록하고 `toneMappingFunctionIndex`로 런타임 스위칭

### 5.6 수중 시각화의 일관성

**문제**: 수중에서는 색감이 파랗고 빛이 더 번지는 느낌이 필요한데, 한 군데에서 다 처리하면 너무 강하거나 부자연스러움.

**해결**: 두 단계 분리.

1. **WaterFilter**: 파란 틴트를 알파블렌딩. 색역 자체를 한 톤 깔아준다.
2. **CombineBloom**: `isUnderWater`일 때 `bloomStrength`에 `+0.125`를 더해 **수중에서 살짝 더 번지는 분위기**.

또한 수중 진입 시간(`m_waterAdaptationTime`)에 따라 fog와 filter 모두 점진적으로 강해진다 → 잠수 후 천천히 푸르러지는 효과.

## 6. 결과

- HDR 라이팅의 1.0 초과 정보를 끝까지 보존하여, 톤매핑 단계에서 의미 있는 압축이 일어난다
- 4단계 Bloom 피라미드가 **좁은 글로우 ~ 넓은 글로우**를 동시에 만들고, HG Phase로 **태양 방향에서만 강하게** 부풀어 오른다
- White-Preserving Luma-Based Reinhard로 채도와 하이라이트가 동시에 살아남는 톤
- Fog는 MSAA 샘플 단위로 적용되어 가장자리에서도 부드러운 깊이감
- Water 진입 시 점진적으로 파란 빛 + 안개 + Bloom이 강화되며 수중 분위기가 형성

## 7. 회고

- **자동 노출 부재**: `exposure = 1.5` 고정이라 어두운 실내↔밝은 실외 같은 큰 휘도 변화에서 눈의 적응을 표현하지 못한다. 평균/로그-평균 휘도를 다운샘플로 추정해 EV를 동적으로 조정하면 자연스러워질 것.
- **Bloom Progressive Upsampling 부재**: 현재 Up 단계가 “저해상도 → 두 배 확대”만 한다. 각 Up 패스에 같은 해상도의 Down 결과를 더해(Add Blend) 누적하면 좀 더 풍부한 글로우가 가능하다.
- **Tone Mapping 단순화 여지**: 7개 등록은 비교용으로는 좋지만, 최종 포트폴리오 빌드에서는 하나(또는 ACES)로 좁히고 색 그레이딩(LUT 등)으로 마무리하는 편이 자연스럽다.
- **Threshold 토글**: 풍경/스타일에 따라 Threshold가 더 어울리는 장면이 있어, 런타임 토글 정도는 두는 게 비교에 좋을 듯하다.
- **Fog와 PostEffect 일관성**: 현재 Fog만 Forward 안쪽에 들어가 있다. MSAA를 후처리 전 단계에서 미리 Resolve하거나, 모든 후처리를 MSAA 안에서 끝내는 방향으로 통일해두면 파이프라인이 더 깔끔해질 것이다.
