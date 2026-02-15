# Post Effect — HDR 렌더링, Bloom, Tone Mapping

## 1. 개요

Voxen의 Post Effect 파이프라인은 Deferred/Forward 셰이딩이 끝난 **HDR 이미지**를 입력으로, **Bloom(빛 번짐)** 과 **Tone Mapping(HDR→LDR 변환)** 을 수행하여 최종 화면을 출력한다.

핵심 구성:

- **HDR 렌더링** — 라이팅 결과를 `R16G16B16A16_FLOAT` 포맷으로 기록하여, 1.0을 초과하는 밝기 정보를 보존
- **Bloom** — 밝은 영역의 빛이 주변으로 번지는 효과. 4단계 다운샘플링 + 4단계 업샘플링의 멀티 패스 구조
- **Tone Mapping** — HDR 색상을 디스플레이가 표현할 수 있는 `[0, 1]` LDR 범위로 변환하고 감마 보정 적용

## 2. 도입 동기

### HDR의 필요성

라이팅 계산에서 태양의 `radianceWeight`는 최대 2.0이고, Specular BRDF는 이를 더 증폭시킬 수 있다. 만약 렌더 타겟이 `[0, 1]` LDR이면, 1.0을 초과하는 밝기 정보가 잘려나가(Clamp) **밝은 영역이 모두 순백색으로 뭉개진다**. 이는 태양 하이라이트, 금속 반사, 발광 블록의 밝기 차이를 표현할 수 없게 만든다.

HDR 렌더 타겟(`R16G16B16A16_FLOAT`)은 이론적으로 65504까지의 값을 저장할 수 있어, 라이팅 단계의 에너지 보존이 유지된다.

### Bloom의 필요성

현실에서 강한 빛은 렌즈 플레어나 눈부심으로 주변에 번진다. HDR 렌더링만으로는 밝은 영역이 "숫자상" 밝을 뿐, 시각적으로 빛나 보이지는 않는다. Bloom은 HDR의 **밝은 영역을 추출하고 블러하여** 주변에 퍼뜨림으로써, 실제로 "빛나는" 느낌을 만든다.

### Tone Mapping의 필요성

모니터는 `[0, 1]` 범위만 표현할 수 있으므로, HDR 값을 LDR로 매핑해야 한다. 단순 Clamp가 아닌 **톤 매핑 커브**를 적용하여, 밝은 영역의 디테일을 최대한 보존하면서 자연스럽게 압축한다.

## 3. 핵심 아이디어

### 3.1 멀티 패스 Bloom

Bloom의 핵심은 **다양한 크기의 빛 번짐을 효율적으로 생성**하는 것이다. 단일 해상도에서 넓은 블러를 하면 매우 큰 커널이 필요하여 비효율적이다.

대신 **다운샘플링 → 업샘플링** 피라미드를 구성한다:

```
Down (해상도 줄이기)           Up (해상도 복원)
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

[원본]  ──→  [1/2]  ──→  [1/4]  ──→  [1/8]  ──→  [1/16]
                                                     │
[결과]  ←──  [1/2]  ←──  [1/4]  ←──  [1/8]  ←───────┘
```

- **다운샘플링**: 해상도를 절반씩 줄이면서 가중 평균 필터를 적용. 저해상도에서의 1텍셀 블러 = 고해상도에서의 넓은 블러
- **업샘플링**: 해상도를 두 배씩 복원하면서 가중 평균으로 부드럽게 확장
- 각 단계의 결과가 누적되어, **좁은 번짐(근거리)부터 넓은 번짐(원거리)까지** 자연스럽게 합쳐진다

### 3.2 Linear Tone Mapping + 감마 보정

Voxen은 가장 단순한 **Linear Tone Mapping**을 사용한다:

```
color = clamp(exposure × color, 0, 1)
color = pow(color, 1/2.2)    ← 감마 보정
```

`exposure`(노출값)로 전체 밝기를 조절한 뒤 클램프하고, 최종적으로 **역감마(1/2.2)** 를 적용하여 모니터의 감마 특성에 맞춘다. 전체 라이팅 파이프라인이 Linear 공간에서 수행되었으므로, 이 단계에서 디스플레이용 sRGB 공간으로 변환하는 것이다.

### 3.3 Henyey-Greenstein 기반 Bloom 강도

Bloom의 강도가 고정값이면 모든 방향에서 동일하게 빛이 번진다. 실제로는 카메라가 **태양을 바라볼 때** 빛 번짐이 가장 강하다(렌즈 플레어 효과).

이를 위해 **Henyey-Greenstein Phase Function**을 사용한다. 이 함수는 원래 대기 산란에서 빛의 전방 산란(Forward Scattering) 확률을 모델링하는 것으로, 카메라 방향과 태양 방향이 일치할수록 높은 값을 반환한다. 이를 Bloom 강도에 더하여, **태양 방향을 볼 때 Bloom이 강해지는** 자연스러운 효과를 만든다.

## 4. 구현 내용

### 4.1 HDR 렌더 타겟 구성 (Graphics.cpp)

```cpp
// basic buffer — 라이팅 출력 대상
DXGI_FORMAT format = DXGI_FORMAT_R16G16B16A16_FLOAT;
DXUtils::CreateTextureBuffer(basicBuffer, APP_WIDTH, APP_HEIGHT, false, format, bindFlag);

// bloom buffer[5] — 피라미드 체인 (원본 ~ 1/16 해상도)
format = DXGI_FORMAT_R16G16B16A16_FLOAT;
UINT bloomWidth = APP_WIDTH, bloomHeight = APP_HEIGHT;
for (int i = 0; i < 5; ++i) {
    DXUtils::CreateTextureBuffer(bloomBuffer[i], bloomWidth, bloomHeight, ...);
    bloomWidth /= 2;
    bloomHeight /= 2;
}
```

| 버퍼 | 해상도 | 포맷 | 용도 |
|---|---|---|---|
| `basicBuffer` | 원본 | R16G16B16A16_FLOAT | Deferred/Forward 셰이딩 결과 (HDR) |
| `bloomBuffer[0]` | 원본 | R16G16B16A16_FLOAT | Bloom 결과 (업샘플링 최종) |
| `bloomBuffer[1]` | 1/2 | R16G16B16A16_FLOAT | 다운/업 중간 단계 |
| `bloomBuffer[2]` | 1/4 | R16G16B16A16_FLOAT | 다운/업 중간 단계 |
| `bloomBuffer[3]` | 1/8 | R16G16B16A16_FLOAT | 다운/업 중간 단계 |
| `bloomBuffer[4]` | 1/16 | R16G16B16A16_FLOAT | 다운샘플링 최저 해상도 |
| `backBuffer` | 원본 | Swap Chain 포맷 | 최종 LDR 출력 (디스플레이) |

모든 중간 버퍼가 `R16G16B16A16_FLOAT`인 것이 중요하다. Bloom 과정에서도 HDR 정보가 손실 없이 유지되어야, 1.0 초과 밝기의 번짐이 정확히 표현된다.

### 4.2 Bloom Down — 가중 다운샘플링 (BloomDownPS.hlsl)

```hlsl
// 13-tap 가중 필터 (5개 겹치는 2×2 블록)
// a - b - c    j k l m = 0.5    / 4
// - j - k -    a b d e = 0.125  / 4
// d - e - f    b c e f = 0.125  / 4
// - l - m -    d e g h = 0.125  / 4
// g - h - i    e f h i = 0.125  / 4

color += (j + k + l + m) * 0.5 * 0.25;
color += (a + b + d + e) * 0.125 * 0.25;
color += (b + c + e + f) * 0.125 * 0.25;
color += (d + e + g + h) * 0.125 * 0.25;
color += (e + f + h + i) * 0.125 * 0.25;
```

단순한 2×2 평균 다운샘플 대신, **5개의 겹치는 2×2 블록을 가중 합산**하는 13-tap 필터를 사용한다.

#### 샘플 배치와 가중치

```
a - b - c       ← 짝수 간격 (2dx, 2dy) 떨어진 9개 점
- j - k -       ← 홀수 간격 (1dx, 1dy) 떨어진 4개 점
d - e - f
- l - m -
g - h - i
```

- `j, k, l, m` (중앙 4점): 가중치 `0.5/4 = 0.125` 씩 — 가장 높은 비중
- `a,b,d,e` / `b,c,e,f` / `d,e,g,h` / `e,f,h,i` (모서리 4블록): 각 `0.125/4 = 0.03125` 씩

이 필터는 **텐트(tent) 형태의 가중치 분포**를 만든다. 중심 `e`는 모서리 4블록 모두에 포함되어 가장 높은 누적 가중치를 갖고, 가장자리 `a, c, g, i`는 하나의 블록에만 포함되어 가장 낮다. 이로써 Box Filter(균일 평균)보다 부드러운 다운샘플링이 가능하며, **Firefly(밝은 점 하나가 넓게 퍼지는 아티팩트)를 억제**한다.

#### 4단계 다운샘플 체인

```cpp
// PostEffect::Bloom() — Down
for (int i = 0; i <= 3; ++i) {
    int div = (int)pow(2, i + 1);
    // 뷰포트: APP_WIDTH/div × APP_HEIGHT/div
    // 입력: i==0 ? basicSRV : bloomSRV[i]
    // 출력: bloomRTV[i+1]
}
```

| 패스 | 입력 | 출력 | 출력 해상도 |
|---|---|---|---|
| Down 0 | `basicSRV` (원본 HDR) | `bloomRTV[1]` | 1/2 |
| Down 1 | `bloomSRV[1]` | `bloomRTV[2]` | 1/4 |
| Down 2 | `bloomSRV[2]` | `bloomRTV[3]` | 1/8 |
| Down 3 | `bloomSRV[3]` | `bloomRTV[4]` | 1/16 |

첫 번째 패스의 입력이 `basicSRV`(셰이딩 결과)인 점에 주목 — 별도의 Brightness Threshold(밝기 추출) 없이, **전체 이미지를 다운샘플**한다. HDR 값이 높은 영역은 다운샘플 후에도 높은 값을 유지하므로, 자연스럽게 밝은 부분이 Bloom의 주요 소스가 된다.

### 4.3 Bloom Up — 가중 업샘플링 (BloomUpPS.hlsl)

```hlsl
// 3×3 가중 평균 (1-2-1 커널)
// a - b - c   →  1  2  1
// d - e - f   →  2  4  2   → /16
// g - h - i   →  1  2  1

color += (a + c + g + i);           // 모서리: ×1
color += (b + d + f + h) * 2.0;     // 변: ×2
color += e * 4.0;                    // 중심: ×4
color /= 16.0;
```

업샘플링에서는 **3×3 텐트 필터(1-2-1 가중치)**를 사용한다. 이는 `[1,2,1] ⊗ [1,2,1]`의 분리 가능 커널과 동일한 결과로, 바이리니어 보간과 유사하되 더 넓은 영역을 커버한다.

`linearClampSS` 샘플러를 사용하므로, 저해상도 텍스처에서 읽을 때 하드웨어 바이리니어 보간이 한 번 더 적용된다. 즉, **소프트웨어 텐트 필터 + 하드웨어 바이리니어**의 이중 평활화로 업스케일 시 블록 아티팩트가 방지된다.

#### 4단계 업샘플 체인

```cpp
// PostEffect::Bloom() — Up
for (int i = 3; i >= 0; --i) {
    int div = (int)pow(2, i);
    // 뷰포트: APP_WIDTH/div × APP_HEIGHT/div
    // 입력: bloomSRV[i+1]
    // 출력: bloomRTV[i]
}
```

| 패스 | 입력 | 출력 | 출력 해상도 |
|---|---|---|---|
| Up 0 | `bloomSRV[4]` (1/16) | `bloomRTV[3]` | 1/8 |
| Up 1 | `bloomSRV[3]` (1/8) | `bloomRTV[2]` | 1/4 |
| Up 2 | `bloomSRV[2]` (1/4) | `bloomRTV[1]` | 1/2 |
| Up 3 | `bloomSRV[1]` (1/2) | `bloomRTV[0]` | 원본 |

최종 `bloomSRV[0]`에는 **모든 해상도 단계의 블러가 누적된** Bloom 결과가 담긴다. 1/16 해상도에서의 블러는 원본 기준으로 매우 넓은 반경의 번짐에 해당하고, 1/2 해상도에서의 블러는 좁은 번짐에 해당한다.

### 4.4 Combine & Tone Mapping (CombineBloomPS.hlsl)

#### Henyey-Greenstein Phase Function

```hlsl
float henyeyGreensteinPhase(float3 L, float3 V, float aniso)
{
    float cosT = dot(L, V);
    float g = aniso;
    return (1.0 - g * g) / (4.0 * PI * pow(abs(1.0 + g * g - 2.0 * g * cosT), 3.0 / 2.0));
}
```

**Henyey-Greenstein(HG) 함수**는 산란 매질에서 빛이 특정 방향으로 산란되는 확률 분포를 모델링한다.

- `L` = 태양 방향(`lightDir`), `V` = 카메라 방향(`eyeDir`)
- `aniso = 0.9` — 비등방성 파라미터. 1.0에 가까울수록 **전방 산란(Forward Scattering)**이 지배적
- `cosT = dot(L, V)` — 카메라가 태양을 정면으로 바라보면 1.0, 등지면 -1.0

`aniso = 0.9`에서 `cosT → 1.0`이면 HG 값이 급격히 증가한다. 즉, **태양을 직접 바라볼 때 Bloom 강도가 크게 올라가** 렌즈 플레어와 유사한 빛 번짐을 만든다.

#### Bloom 합성과 Tone Mapping

```hlsl
float scattering = min(henyeyGreensteinPhase(lightDir, eyeDir, 0.9), 1.0) * 0.25;
float bloomStrength = scattering + (isUnderWater ? 0.25 : 0.1);

float3 combineColor = lerp(renderColor, bloomColor, bloomStrength);

return float4(linearToneMapping(combineColor, 1.0), 1.0);
```

1. **Bloom 강도 계산**:
   - 기본 강도: 지상 `0.1`, 수중 `0.25` — 수중에서는 빛이 더 퍼지므로 Bloom이 강하다
   - 산란 보너스: HG 함수 결과 × `0.25` — 태양 방향에서 최대 +0.25
   - 최종 `bloomStrength` 범위: 지상 `[0.1, 0.35]`, 수중 `[0.25, 0.5]`

2. **합성**: `lerp(renderColor, bloomColor, bloomStrength)`
   - `bloomStrength`만큼 원본 색상을 Bloom 색상으로 대체
   - 값이 0.1이면 원본 90% + Bloom 10%, 0.35이면 원본 65% + Bloom 35%

3. **Linear Tone Mapping**:
   ```hlsl
   float3 linearToneMapping(float3 color, float exposure)
   {
       float3 invGamma = float3(1, 1, 1) / 2.2;
       color = clamp(exposure * color, 0.0, 1.0);
       color = pow(color, invGamma);
       return color;
   }
   ```
   - `exposure = 1.0` — 현재 고정 노출. HDR 값에 노출을 곱한 뒤 `[0, 1]`로 클램프
   - `pow(color, 1/2.2)` — **감마 보정**. Linear 공간의 색상을 sRGB(디스플레이) 공간으로 변환
   - 이 과정을 거치면 어두운 영역의 디테일이 보존되고, 밝은 영역은 부드럽게 압축된다

### 4.5 전체 파이프라인 요약

```
[Deferred + Forward Shading]
라이팅 결과 → basicBuffer (R16G16B16A16_FLOAT, HDR)
                    │
                    ▼
[Bloom Down] ─────────────────────────────────
basicSRV → bloomRTV[1] (1/2)   13-tap 가중 필터
         → bloomRTV[2] (1/4)
         → bloomRTV[3] (1/8)
         → bloomRTV[4] (1/16)
                    │
                    ▼
[Bloom Up] ──────────────────────────────────
bloomSRV[4] → bloomRTV[3] (1/8)   3×3 텐트 필터
            → bloomRTV[2] (1/4)
            → bloomRTV[1] (1/2)
            → bloomRTV[0] (원본)
                    │
                    ▼
[Combine & Tone Mapping] ───────────────────
basicSRV (원본 HDR) + bloomSRV[0] (Bloom)
        ↓
HG Phase → bloomStrength (태양 방향 = 강한 Bloom)
        ↓
lerp(render, bloom, strength)
        ↓
linearToneMapping(exposure=1.0) + 감마 보정(1/2.2)
        ↓
backBufferRTV (LDR, 디스플레이 출력)
```

## 5. 문제점 & 해결

### 5.1 Bloom의 Firefly Artifact

**문제**: HDR 이미지에서 1~2픽셀의 극도로 밝은 점(예: Specular 하이라이트)이 다운샘플 시 넓은 영역에 영향을 미쳐, 비정상적으로 큰 빛 번짐이 생긴다.

**해결**: BloomDownPS의 13-tap 가중 필터가 이를 완화한다. 중앙 4점(`j,k,l,m`)에 높은 가중치, 모서리 블록에 낮은 가중치를 부여하는 텐트 분포는, 단일 밝은 점의 영향을 **주변 텍셀과의 가중 평균으로 희석**시킨다. Box Filter보다 Firefly 억제력이 높다.

### 5.2 Bloom 강도의 방향 무관성

**문제**: Bloom 강도가 고정이면 태양 반대편을 봐도 동일하게 빛이 번져 부자연스럽다.

**해결**: Henyey-Greenstein Phase Function을 도입하여, 카메라-태양 방향의 각도에 따라 Bloom 강도를 동적으로 조절한다. `aniso = 0.9`로 강한 전방 산란을 설정하여, 태양 방향에서만 Bloom이 크게 증가한다.

### 5.3 트레이드오프 — Linear Tone Mapping의 한계

Linear Tone Mapping은 구현이 단순하지만, 밝은 영역의 디테일 보존 측면에서 **Reinhard, ACES, Filmic 등의 커브 기반 Tone Mapping**보다 열위다. `clamp(color, 0, 1)`로 1.0 초과 값을 단순 절삭하기 때문에, HDR의 밝은 영역에서 디테일이 손실될 수 있다.

현재 `exposure = 1.0` 고정이므로, 어두운 실내에서 밝은 실외로 나갈 때 자동 노출 조절이 없다. 이는 단순함을 위한 의도적 선택이다.

## 6. 결과

- 태양 하이라이트, 금속 반사, 수면 반사 등 HDR 영역에서 **자연스러운 빛 번짐(Bloom)** 이 생긴다
- 4단계 다운/업샘플 피라미드로 **좁은 글로우부터 넓은 글로우까지** 다양한 크기의 번짐이 합성된다
- HG Phase Function으로 **태양을 바라볼 때 Bloom이 강해지는** 렌즈 플레어 효과를 얻는다
- 수중에서는 Bloom 강도가 높아져 **탁한 물속에서 빛이 퍼지는 느낌**을 표현한다
- 감마 보정(1/2.2)으로 Linear 공간 라이팅이 sRGB 디스플레이에 정확히 출력된다

## 7. 회고

- **Tone Mapping 개선 여지**: Linear 대신 ACES Filmic이나 Uncharted 2 Tone Mapping을 적용하면, 밝은 영역의 디테일이 더 잘 보존되고 영화적 색감을 얻을 수 있다
- **자동 노출(Auto Exposure)**: 현재 `exposure = 1.0` 고정이지만, 화면 평균 휘도를 계산하여 노출을 동적으로 조절하면 어두운 실내→밝은 실외 전환 시 눈의 적응 효과를 표현할 수 있다
- **Bloom Threshold**: 현재 전체 이미지를 다운샘플하는 방식이므로 어두운 영역도 미세하게 번진다. 밝기 임계값(Threshold)을 두어 일정 밝기 이상만 Bloom 소스로 사용하면 더 선택적인 빛 번짐이 가능하다
- **다운샘플과 업샘플에서 이전 단계 누적 부재**: 현재 업샘플 시 해당 해상도의 다운샘플 결과를 더하지 않고 순수하게 저해상도만 확대한다. 각 단계에서 해당 해상도의 다운샘플 결과를 누적하면(Progressive Upsampling) 더 풍부한 Bloom 디테일을 얻을 수 있다
