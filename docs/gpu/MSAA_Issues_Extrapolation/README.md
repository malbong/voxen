# MSAA Issues — Greedy Meshing 환경에서의 Bleeding과 Mipmap 문제

## 1. 개요

Voxel 렌더링에서 Greedy Meshing과 Bit-packed 정점 데이터를 사용할 때, 4x MSAA 환경에서 발생하는 두 가지 문제와 그 해결을 다룬다.

1. **Bleeding** — 보간된 텍스처 좌표가 삼각형 밖으로 Extrapolation되어 인접 블록의 텍스처가 번져 보이는 현상
2. **잘못된 Mipmap 선택** — 텍스처 좌표의 `frac()` 연산이 만드는 불연속점에서 GPU가 과도하게 높은 Mipmap 레벨을 선택하여 텍스처가 흐려지는 현상

두 문제 모두 **`sample` Interpolation**을 핵심으로, Wrap Sampler와 조합하여 해결한다.

## 2. 도입 동기

### Greedy Meshing과 텍스처 좌표

Greedy Meshing은 동일한 블록 타입의 인접 면을 하나의 큰 사각형으로 병합한다. 
예를 들어 5×3 블록이 병합되면 단 2개의 삼각형으로 표현되어 정점/인덱스 수가 크게 줄지만, 텍스처 좌표 범위가 `[0, 5] × [0, 3]`처럼 1.0을 초과한다.

```
[일반 메싱]          [Greedy Meshing]
┌─┬─┬─┬─┬─┐         ┌─────────────┐
│ │ │ │ │ │         │             │
├─┼─┼─┼─┼─┤         │  하나의 Quad │
│ │ │ │ │ │         │  UV: 0~5    │
├─┼─┼─┼─┼─┤         │             │
│ │ │ │ │ │         │             │
└─┴─┴─┴─┴─┘         └─────────────┘
 15개 Quad             1개 Quad
```

이 큰 텍스처 좌표는 Wrap 샘플러로 반복 처리되어야 하는데, MSAA 환경에서 보간 방식에 따라 Bleeding과 Mipmap 문제가 발생한다.

### Bit-packed 정점 데이터

정점 데이터는 메모리 절약을 위해 `uint32_t` 하나에 위치(x, y, z 각 6비트), face(3비트), texIndex(8비트)를 Bit-pack한다.

```hlsl
// BasicVS.hlsl
int x = (input.data >> 23) & 63;  // 6bit
int y = (input.data >> 17) & 63;  // 6bit
int z = (input.data >> 11) & 63;  // 6bit
uint face = (input.data >> 8) & 7;
uint texIndex = input.data & 255;
```

텍스처 좌표는 VS에서 위치와 면 방향으로부터 계산된다:

```hlsl
// Common.hlsli — getVoxelTexcoord()
if (face == 3) // top
    texcoord = float2(pos.x, -pos.z + 32.0);
// Greedy Meshing으로 pos.x가 0~5이면, texcoord.x도 0~5
```

## 3. 핵심 아이디어

두 문제의 공통 원인은 **MSAA에서 PS 입력 값이 어디서, 어떻게 보간되는가**이다. HLSL은 세 가지 보간 모드를 제공하며, 각각의 특성이 문제 해결에 직접적으로 연관된다.

| 보간 모드 | 평가 위치 | 보간값 공유 | Extrapolation 가능 |
|---|---|---|---|
| `linear` (기본) | 픽셀 중심 | 4샘플 모두 동일값 | **가능** |
| `centroid` | 커버된 샘플들의 무게중심 | 4샘플 모두 동일값 | 불가능 |
| `sample` | 각 샘플 위치 | **샘플마다 독립** | 불가능 |

이 프로젝트의 해결 방식:
- **`sample` Interpolation** — Extrapolation 방지 + 샘플별 독립 보간
- **`frac()` 제거 + Wrap Sampler** — Mipmap 불연속 제거

## 4. 구현 내용

### 4.1 문제 1: Bleeding — Extrapolation

#### 문제 상황

4x MSAA에서 삼각형 가장자리의 픽셀은 일부 샘플만 삼각형에 덮인다. 기본 `linear` 보간은 **픽셀 중심**에서 보간값을 계산하는데, 에지 픽셀의 중심은 삼각형 바깥에 놓일 수 있다.

```
┌─────────────┐
│  ○ Sample 0 │  ← 삼각형 안 (커버됨)
│      ╳      │  ← 픽셀 중심 (삼각형 밖!)
│  ○ Sample 2 │  ← 삼각형 밖 (커버 안 됨)
│  ○ Sample 3 │  ← 삼각형 밖 (커버 안 됨)
└─────────────┘
╳ = linear 보간 위치 (삼각형 밖 → Extrapolation!)
```

삼각형 밖에서 보간하면 정점 값이 **Extrapolation**(외삽)되어, 텍스처 좌표가 원래 블록 범위를 벗어난다. 예를 들어 texcoord가 -0.1이나 5.1이 되면 인접 블록의 텍스처가 비쳐 Bleeding이 발생한다.

#### Centroid가 충분하지 않은 이유

`centroid` 보간은 커버된 샘플들의 무게중심에서 보간하므로 Extrapolation은 방지된다. 그러나 **4개 샘플 모두 동일한 보간값을 받는다**는 치명적 한계가 있다.

```
[centroid 보간]
Sample 0: texcoord = (2.3, 1.7)  ← centroid 위치의 값
Sample 1: texcoord = (2.3, 1.7)  ← 동일!
Sample 2: texcoord = (2.3, 1.7)  ← 동일!
Sample 3: texcoord = (2.3, 1.7)  ← 동일!
→ Point Sampler로 샘플링하면 4개 모두 같은 텍셀 → MSAA 효과 없음
```

이 프로젝트는 텍스처 샘플링에 `pointWrapSS`(Point Sampler)를 사용한다. Point Sampler는 보간값이 조금만 달라도 다른 텍셀을 샘플링할 수 있어 서브 픽셀 수준의 디테일을 제공하는데, centroid에서는 모든 샘플이 같은 좌표를 받으므로 이 장점이 완전히 사라진다.

#### 해결: `sample` Interpolation

`sample` 보간은 **각 샘플 위치에서 독립적으로** 보간값을 계산한다.

```
[sample 보간]
Sample 0: texcoord = (2.25, 1.75)  ← Sample 0 위치에서 보간
Sample 1: texcoord = (2.75, 1.75)  ← Sample 1 위치에서 보간
Sample 2: texcoord = (2.25, 1.25)  ← Sample 2 위치에서 보간
Sample 3: texcoord = (2.75, 1.25)  ← Sample 3 위치에서 보간
→ 각 샘플이 독립 좌표 → Point Sampler로 다른 텍셀 가능 → MSAA 유효
```

커버된 샘플의 위치는 항상 삼각형 안이므로 Extrapolation이 발생하지 않으며, 각 샘플이 독립적 보간값을 갖기에 Point Sampler와 결합해도 MSAA가 정상 동작한다.

```hlsl
// BasicVS.hlsl — VS 출력에 sample 지정
struct vsOutput
{
    float4 posProj : SV_POSITION;
    sample float3 posWorld : POSITION;
    sample float3 normal : NORMAL;
    sample float2 texcoord : TEXCOORD;
    uint texIndex : INDEX;
};

// BasicPS.hlsl — PS 입력에도 동일하게 sample 지정
struct psInput
{
    float4 posProj : SV_POSITION;
    sample float3 posWorld : POSITION;
    sample float3 normal : NORMAL;
    sample float2 texcoord : TEXCOORD;
    uint texIndex : INDEX;
};
```

`sample` 키워드가 지정되면 PS가 **픽셀당 1회가 아닌 샘플당 1회** 실행된다. 이는 셰이딩 비용 증가를 의미하지만, DeferredShading_MSAA에서 구현한 Edge/NonEdge Stencil 분기를 통해 에지 픽셀에서만 이 비용이 발생하도록 제한한다.

### 4.2 문제 2: 잘못된 Mipmap 선택

#### 문제 상황

GPU는 텍스처 샘플링 시 스크린 공간 미분(ddx/ddy)으로 Mipmap 레벨을 결정한다. Greedy Meshing으로 texcoord가 `[0, 5]`인 면에서 `frac()`을 적용하면:

```
인접 픽셀 A: frac(0.999) = 0.999
인접 픽셀 B: frac(1.001) = 0.001

ddx = |0.001 - 0.999| = 0.998  ← 실제 변화는 0.002인데 0.998로 계산!
```

GPU는 이 거대한 미분값을 보고 매우 높은 Mipmap 레벨(작고 흐린 텍스처)을 선택한다. 결과적으로 정수 경계를 지나는 모든 위치에서 텍스처가 뿌옇게 보인다.

이 프로젝트의 `pointWrapSS` 샘플러는 `D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR`로 설정되어 있다. MIN/MAG는 Point이지만 **Mip 선택은 Linear**이므로, ddx/ddy에 의한 Mipmap 레벨 선택이 여전히 작동한다.

#### 해결: `frac()` 제거 + Wrap Addressing

텍스처 좌표에 `frac()`을 적용하지 않고, **Wrap Sampler**가 주소 반복을 처리하게 한다.

```hlsl
// getVoxelTexcoord()의 결과를 frac() 없이 그대로 사용
output.texcoord = getVoxelTexcoord(position, face);
// texcoord = (0.0 ~ 32.0) 범위 그대로 PS에 전달

// PS에서도 frac() 없이 바로 샘플링
float4 albedo = blockAtlasTextureArray.Sample(pointWrapSS, float3(texcoord, texIndex));
```

Wrap 모드(`D3D11_TEXTURE_ADDRESS_WRAP`)는 텍스처 좌표의 정수 부분을 GPU 하드웨어 레벨에서 무시한다. `frac()`과 달리 이 과정은 **미분 계산 이후**에 일어나므로 ddx/ddy에 영향을 주지 않는다.

```
Wrap 모드:
인접 픽셀 A: texcoord = 0.999 → Wrap 후 0.999
인접 픽셀 B: texcoord = 1.001 → Wrap 후 0.001
ddx는 원본 좌표 기준: |1.001 - 0.999| = 0.002  ← 올바른 미분값!
```

#### `sample` Interpolation의 추가 효과

`sample` 보간은 Mipmap 문제에도 기여한다. `linear` 보간에서는 픽셀 중심에서 계산된 하나의 텍스처 좌표로 ddx/ddy를 구하는데, 에지 픽셀에서 Extrapolation된 좌표는 비정상적인 미분값을 만들 수 있다.

`sample` 보간에서는 각 샘플이 삼각형 내부의 자신의 위치에서 독립적으로 보간되므로, 미분값이 항상 정상 범위에 놓인다.

### 4.3 최종 코드 구조 요약

```
[VS — BasicVS.hlsl]
uint32_t data → position, face, texIndex 디코딩
                 ↓
getVoxelTexcoord(position, face)  →  texcoord (0~32, frac 없음)
                 ↓
sample float2 texcoord  ← sample 보간 지정

[PS — BasicPS.hlsl]
sample float2 texcoord  ← 샘플별 독립 보간값 수신
                 ↓
blockAtlasTextureArray.Sample(pointWrapSS, float3(texcoord, texIndex))
  │                                 │
  ├── Point MIN/MAG: 텍셀 경계에서 올바른 이산 샘플링
  ├── Wrap Address:   frac() 없이 하드웨어가 좌표 반복 처리
  └── Linear MIP:    연속적 좌표 → 올바른 ddx/ddy → 정확한 Mip 선택
```

## 5. 문제점 & 해결

### 5.1 `sample` 보간의 성능 비용

**문제**: `sample` 보간을 사용하면 PS가 샘플당 1회 실행되어 4x MSAA 기준 최대 4배의 셰이딩 비용이 발생한다.

**해결**: DeferredShading_MSAA에서 구현한 Stencil 기반 Edge/NonEdge 분기와 결합한다. G-Buffer Fill 단계에서 `sample` 보간으로 모든 픽셀이 샘플당 실행되지만, 이후 SSAO와 Shading에서는 Edge 픽셀만 멀티샘플 처리하므로 전체적인 셰이딩 비용 증가는 에지 비율에 비례한다.

### 5.2 Centroid vs Sample 선택의 트레이드오프

**트레이드오프**: `centroid`는 Extrapolation을 방지하면서 PS를 픽셀당 1회만 실행하므로 성능이 좋다. 하지만 Point Sampler와 조합 시 서브 픽셀 해상도가 사라져 MSAA 안티에일리어싱 효과를 잃는다.

**결론**: 복셀 텍스처는 `pointWrapSS`(Point Sampling)로 샘플링하므로, centroid의 동일 좌표 문제가 치명적이다. `sample` 보간으로 샘플별 독립 좌표를 보장하여 Point Sampler에서도 MSAA가 유효하게 동작하도록 했다.

## 6. 결과

- `sample` 보간으로 Extrapolation이 원천 차단되어, Greedy Meshing으로 병합된 큰 면의 에지에서도 Bleeding이 발생하지 않는다
- `frac()` 제거 + Wrap Sampler 조합으로 텍스처 좌표의 미분이 연속적이 되어, 정수 경계에서의 Mipmap 뿌옇게 보이는 현상이 해결된다
- Point Sampler와 `sample` 보간의 조합으로, 에지 픽셀에서 각 서브 샘플이 독립적으로 올바른 텍셀을 참조하여 MSAA 안티에일리어싱이 정상 동작한다

## 7. 회고

- `sample` 보간은 G-Buffer Fill에서 모든 픽셀에 적용되므로, 에지가 아닌 픽셀에서도 샘플당 PS가 실행되는 비용이 있다. G-Buffer 단계에서도 Edge/NonEdge를 사전 분류할 수 있다면 비용을 더 줄일 수 있을 것이다
- 현재 `pointWrapSS`의 Mip Filter가 `LINEAR`로 설정되어 있어 Mipmap 레벨 간 보간이 일어난다. 복셀 아트 스타일에서는 `MIP_POINT`로 변경하여 완전한 이산 텍셀을 유지하는 것도 고려할 수 있다
- Greedy Meshing 없이 블록마다 개별 Quad를 사용하면 texcoord가 항상 `[0, 1]`이라 이 문제가 발생하지 않지만, 정점 수가 수십 배 증가한다. Greedy Meshing의 메모리/성능 이점이 MSAA 처리 복잡도 증가를 충분히 상쇄한다
