# MSAA 환경에서의 Texture Seams 문제 해결

<img width="1600" height="1000" alt="Image" src="https://github.com/user-attachments/assets/95c2e275-b850-46a5-9299-7e5388d55fc5" />

<img width="1600" height="800" alt="Image" src="https://github.com/user-attachments/assets/628e9931-1bb6-41ae-9918-07f0748120a3" />

## 1. 개요

Chunk 렌더링에서 발생했던 Texturing Seams 문제발생과 해결을 문서로 정리했다.

## 2. 문제점

<img width="918" height="510" alt="Image" src="https://github.com/user-attachments/assets/c00e2b55-e86f-436f-aca8-e8392f1b1a60" />

위와 같은 현상이 발생했던 이유는 다음과 같다.

### 2.1 문제 발생의 이유: 이전에 사용했던 Texture Atlas 2D를 그대로 사용

Array 구성 없이 Mipmap 구성하여 높은 Level의 mipmap이 색이 섞이는, 지금 보면 어리석은 결과가 있었다.

<img width="800" height="400" alt="Image" src="https://github.com/user-attachments/assets/a268156d-e462-4987-a2ca-95f7b8aab93d" />

### 2.2 문제 발생의 이유: MSAA Interpolation 방식의 미흡한 이해

문제 해결 과정에서 미흡한 이해가 있었음을 알 수 있었고, 그 전에는 판단할 수 없었다.

### 2.3 문제 발생의 이유: Bit-packed 정점 데이터와 Greedy Meshing 환경에서의 텍스쳐 좌표 연산으로 인한 잘못된 mip level 선택 문제

<img width="500" height="329" alt="Image" src="https://github.com/user-attachments/assets/6729c65a-18e0-479c-b049-0710b83ef735" />

정점 데이터는 메모리 절약을 위해 `uint32_t` 하나에 로컬 위치(x, y, z 각 6비트), face(3비트), texIndex(8비트)를 Bit-pack한다.

```hlsl
// BasicVS.hlsl
struct vsInput
    uint data : DATA;
...
int x = (input.data >> 23) & 63;  // 6bit
int y = (input.data >> 17) & 63;  // 6bit
int z = (input.data >> 11) & 63;  // 6bit
uint face = (input.data >> 8) & 7;
uint texIndex = input.data & 255;
```

Greedy Meshing은 동일한 블록 타입의 인접 면을 하나의 큰 사각형으로 병합한다.
예를 들어 5×3 블록이 병합되면 단 한 개의 쿼드로 표현되어 정점/인덱스 수가 크게 준다.
하지만 그에 필요한 정보(Texture Coordinates)도 줄어 드는 문제가 있었다.

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

텍스쳐 좌표를 연산하는데 VS에 들어온 Bit-packed의 로컬 좌표를 활용하여 텍스쳐 좌표를 연산했다.
VS로 들어온 x,y,z 로컬 위치를 그대로 PS에 보내고, PS에서 Interpolation된 해당 데이터를 가지고 `frac` 연산하여 텍스쳐 좌표로 활용했었다.
그 결과 `Sample()` 과정에서 ddx/ddy가 커지는 문제가 존재했고, 높지 않아도 되는 mip level이 높아지는 문제가 발생했다.
일반적으로는 문제가 없지만, 블록 사이의 경계 부분에서 Seams 문제가 발생했다.

## 3. 문제 해결 방식

### 3.1 해결: 단순히 Texture Atlas Array 구성

메모리 효율은 낮지만 정확한 Mipmap 제공과, Bleeding을 원천 제거하기 위해 Texture Atlas를 적절히 잘라 Texture2DArray를 구성했다.
Texture2DArray 구성 후 `GenerateMips`을 했다.

그 결과 Block 전체가 높은 레벨의 mip을 고르더라도, 다른 Block의 텍스쳐가 섞이는 문제는 발생하지 않았다.

### 3.2 해결: MSAA 환경에서의 Interpolation 방식의 이해

Pixel 아닌 Sample이 Cover되면 PS에서 Invoke가 되는데, Sample의 보간 방식이 픽셀 중심으로 보간된다.
이 때, Sample이 Cover되어도 픽셀 중심이 외부에 존재하는 경우 데이터가 Extrapolation 되버리는 문제 발생했던 것이다.
예를들면, 원래 의도라면 샘플 위치가 `0.99999` 이어야 하는데, 픽셀 중심이 `1.00001` 이라서 샘플 위치도 `1.00001`가 되는 것이다.

보간 방식에 대한 이해가 필요했고, MS에서 찾을 수 있었다.

| 보간 모드       | 평가 위치                | 보간값 공유       | Extrapolation 가능 |
| --------------- | ------------------------ | ----------------- | ------------------ |
| `linear` (기본) | 픽셀 중심                | 4샘플 모두 동일값 | **가능**           |
| `centroid`      | 커버된 샘플들의 무게중심 | 4샘플 모두 동일값 | 불가능             |
| `sample`        | 각 샘플 위치             | **샘플마다 독립** | 불가능             |

[Rasterization Rules - Win32 apps | Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/direct3d11/d3d10-graphics-programming-guide-rasterizer-stage-rules)

<img width="773" height="882" alt="Image" src="https://github.com/user-attachments/assets/83203581-e5a2-4e8e-a06d-a0c77d781806" />

나에게는 메쉬의 Edge 부분에서의 Extrapolation이 문제였기 때문에, `centroid`나 `sample`를 사용하면 됐었다.

성능상의 이유로 `centroid`를 사용했으나 만족하지 못한 결과를 얻었다.

`centroid`가 충분하지 않은 이유는 픽셀이 모두 덮힌 경우 픽셀 중심으로 연산을 하게 되는데, 블록 내부에서도 Texture가 16x16 텍셀로 이루어져있고 이를 Point(Nearest) 샘플을 해야했어서 정확한 텍스쳐 좌표가 필요했다.
그 결과 `sample` 지정자를 사용했다.

```
struct psInput
{
    float4 posProj : SV_POSITION;
    sample float3 posWorld : POSITION;
    sample float3 normal : NORMAL;
    sample float2 texcoord : TEXCOORD;
    uint texIndex : INDEX;
};
```

PS input에 하나의 `sample` 지정자라도 있으면 PS 전체가 비용이 증가하고, 서브샘플 단위로 호출되게 된다.
`sample` 지정자가 있는 것은 sample 기준 Interpolation 하게되고, 해당 지정자가 없으면 픽셀 기준으로 Interpolation 하게 된다.
`sample`이 하나있든, N개 있든 비용은 동일하다.

### 3.3 해결: frac 연산을 사용하지 않음

VS에서 단순히 로컬 좌표를 그대로 활용하여 Texture 좌표를 PS에 넘겨주었다.
Block face에 맞는 방향에 따라 뒤집어 주었다.

그 결과 메쉬 중간에서의 잘못된 mip level을 고르지 않게되고, 경계면에서 색이 섞여 보이는 Seams 문제는 해결되었다.

```
float2 getVoxelTexcoord(float3 pos, uint face)
{
    float2 texcoord = float2(0.0, 0.0);

    if (face == LEFT)
    {
        texcoord = float2(-pos.z + CHUNK_SIZE, -pos.y + CHUNK_SIZE);
    }
    else if (face == RIGHT)
    {
        texcoord = float2(pos.z, -pos.y + CHUNK_SIZE);
    }
    else if (face == BOTTOM)
    {
        texcoord = float2(pos.x, pos.z);
    }
    else if (face == TOP)
    {
        texcoord = float2(pos.x, -pos.z + CHUNK_SIZE);
    }
    else if (face == NEAR)
    {
        texcoord = float2(pos.x, -pos.y + CHUNK_SIZE);
    }
    else // FAR
    {
        texcoord = float2(-pos.x + CHUNK_SIZE, -pos.y + CHUNK_SIZE);
    }

    return texcoord;
}
```

### 4. sample 선택의 트레이드오프 - 비용 문제

sample 지정자를 사용함으로써 렌더링 품질에는 만족하나, 병목이 생기지 않을까 싶었지만 병목은 없었다.
sample 지정자를 사용하는 쉐이더는 라이팅과 같은 무거운 연산이 아닌, 단순히 G-Buffer를 위한 샘플링이 중심이기 때문이다.
추가로 실제 하드웨어에서 동작하는 최적화가 존재한다고 한다.

## 5. 회고

- 문제 해결의 구현 난이도는 매우 낮았지만, 어떤게 문제인지 판단하는데 너무 어려웠던 문제
  - 텍스쳐 좌표가 잘못된 건지, 메쉬가 튀어나오는건지 등등 변수가 너무 많아 디버깅이 매우 어려웠다.
  - MSAA 환경이라 하나의 픽셀에 4개의 샘플이 섞이는 과정이라 더더욱 판단하기 어려웠다.
- MSAA 보간 방식에 대해서 학습할 수 있었던 계기가 되었다.
