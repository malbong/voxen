# Block Bit-Packed Vertex

## 1. 개요

Block Bit-Packed Vertex Voxen 엔진에서 블록 메쉬의 정점 데이터를 **32비트 단일 정수(uint)로 압축**하여 저장하는 메모리 최적화 기법이다.
전통적인 정점 구조(위치 12바이트 + 법선 12바이트 + UV 8바이트 + 텍스처 인덱스 4바이트 = 36바이트)를 **4바이트(32비트)**로 압축하여 **메모리 사용량을 약 90% 감소**시킨다.

**32비트 레이아웃**:

```
Bit:  31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10 09 08 07 06 05 04 03 02 01 00
      [ 미사용 ][    X 6비트    ] [     Y 6비트    ] [    Z 6비트     ][ F 3비트 ][    TexIndex 8비트    ]
```

## 2. 도입 동기

복셀 엔진은 청크 내부의 수 많은 블록들을 렌더링해야했고, 단순히 정점 하나에 36byte를 사용하면 메모리 사용률이 매우 높아진다.
또한 이 정점을 단순히 CPU에서 정보로 가지고 있을 뿐만 아니라, GPU 대역폭에 맞춰 전달해야하기에 최적화했어야 했다.

## 3. 핵심 아이디어

### 3.1. 32비트 레이아웃

모든 정점 데이터를 32비트(4바이트) 단일 정수에 패킹:

**32비트 레이아웃**:

```
Bit:  31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10 09 08 07 06 05 04 03 02 01 00
      [ 미사용 ][    X 6비트    ] [     Y 6비트    ] [    Z 6비트     ][ F 3비트 ][    TexIndex 8비트    ]
```

**필드별 비트 할당**:

| 필드       | 비트 범위 | 비트 수 | 값 범위 | 용도                           |
| ---------- | --------- | ------- | ------- | ------------------------------ |
| X          | 23-28     | 6비트   | 0-63    | 청크 내 X 좌표 (CHUNK_SIZE=32) |
| Y          | 17-22     | 6비트   | 0-63    | 청크 내 Y 좌표                 |
| Z          | 11-16     | 6비트   | 0-63    | 청크 내 Z 좌표                 |
| Face       | 8-10      | 3비트   | 0-7     | 면 방향 (6방향 + 예비 2개)     |
| TexIndex   | 0-7       | 8비트   | 0-255   | 텍스처 아틀라스 인덱스         |
| **미사용** | 29-31     | 3비트   | -       | 향후 확장 (AO, 조명 등)        |

**총 사용 비트**: 6 + 6 + 6 + 3 + 8 = **29비트** (32비트 중 3비트 여유)

### 3.2. 비트 할당

**1. 위치 (X, Y, Z): 각 6비트**

- 청크 크기: 32x32x32 블록
- 필요 범위: 0~32

**2. Face: 3비트**

- 6방향 법선: LEFT(0), RIGHT(1), BOTTOM(2), TOP(3), NEAR(4), FAR(5)
- 3비트 = 8개 표현 가능 → 6방향 + 예비 2개
- 월드 특성상 회전하지 않아 특정 페이스의 노멀벡터만 필요
  - 법선 벡터 `float3`(12바이트) 대신 방향 인덱스만 저장 → 셰이더에서 룩업 테이블로 복원

**3. TexIndex: 8비트**

- 텍스처 아틀라스 인덱스: 0~255
- 256개 블록 타입 지원 (`Block::BLOCK_TYPE_COUNT = 256`)
- 각 블록 타입의 텍스처를 아틀라스에서 직접 인덱싱

**4. UV 좌표: 저장 안 함 (0비트)**

- UV는 Local (X, Y, Z) 좌표로 충분히 구할 수 있었다.

**5. 법선: 저장 안 함 (0비트)**

- 법선은 Face 3Bit를 이용하여 셰이더에서 `getNormal(face)` 함수로 **룩업 테이블** 사용

### 3.3. 비트 패킹 (CPU)

CPU에서 정점 데이터를 32비트로 패킹 (추정):

```cpp
uint32_t PackVertexData(int x, int y, int z, uint8_t face, uint8_t texIndex) {
    uint32_t data = 0;

    data |= ((x & 63) << 23);       // X: 비트 23-28
    data |= ((y & 63) << 17);       // Y: 비트 17-22
    data |= ((z & 63) << 11);       // Z: 비트 11-16
    data |= ((face & 7) << 8);      // Face: 비트 8-10
    data |= (texIndex & 255);       // TexIndex: 비트 0-7

    return data;
}
```

### 3.4. 비트 언패킹 (GPU Shader)

GPU 셰이더에서 32비트 데이터를 각 필드로 분해:

```hlsl
// BasicVS.hlsl
struct vsInput {
    uint data : DATA;  // 32비트 압축 데이터
};

vsOutput main(vsInput input) {
    // 비트 시프트와 마스킹으로 언패킹
    int x = (input.data >> 23) & 63;        // 비트 23-28 추출
    int y = (input.data >> 17) & 63;        // 비트 17-22 추출
    int z = (input.data >> 11) & 63;        // 비트 11-16 추출
    uint face = (input.data >> 8) & 7;      // 비트 8-10 추출
    uint texIndex = input.data & 255;       // 비트 0-7 추출

    // 위치 복원
    float3 position = float3(float(x), float(y), float(z));

    // 법선 복원 (룩업 테이블)
    output.normal = getNormal(face);

    // UV 복원 (절차적 계산)
    output.texcoord = getVoxelTexcoord(position, face);

    // 텍스처 인덱스는 그대로 전달
    output.texIndex = texIndex;

    return output;
}
```

## 4. 구현 내용

### 4.1. CPU 측: 정점 데이터 생성 (Chunk.cpp)

**VoxelVertex 구조체 정의**:

```cpp
struct VoxelVertex {
    uint32_t data;  // 32비트 압축 데이터
};
```

**Input Layout 구성**:

```cpp
std::vector<D3D11_INPUT_ELEMENT_DESC> elementDesc = {
	{ "DATA", 0, DXGI_FORMAT_R32_UINT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
};
```

### 4.2. GPU 측: 정점 셰이더 언패킹 (BasicVS.hlsl)

**Vertex Shader (BasicVS.hlsl)**:

```hlsl
cbuffer ChunkConstantBuffer : register(b0) {
    matrix world;
}

struct vsInput {
    uint data : DATA;
};

struct vsOutput {
    float4 posProj : SV_POSITION;
    float3 posWorld : POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD;
    uint texIndex : INDEX;
};

vsOutput main(vsInput input) {
    vsOutput output;

    // 1. 비트 언패킹
    int x = (input.data >> 23) & 63;
    int y = (input.data >> 17) & 63;
    int z = (input.data >> 11) & 63;
    uint face = (input.data >> 8) & 7;
    uint texIndex = input.data & 255;

    // 2. 위치 복원 (정수 → 실수)
    float3 position = float3(float(x), float(y), float(z));

    // 3. 월드 변환
    output.posProj = mul(float4(position, 1.0), world);
    output.posWorld = output.posProj.xyz;

    // 4. 뷰/투영 변환
    output.posProj = mul(output.posProj, view);
    output.posProj = mul(output.posProj, proj);

    // 5. 법선 복원 (룩업 테이블)
    output.normal = getNormal(face);

    // 6. UV 복원 (절차적 계산)
    output.texcoord = getVoxelTexcoord(position, face);

    // 7. 텍스처 인덱스 전달
    output.texIndex = texIndex;

    return output;
}
```

### 4.3. 법선 복원: getNormal() 함수 (BasicVS.hlsl)

```hlsl
float3 getNormal(uint face) {
    static const float3 normals[6] =
    {
        float3(-1, 0, 0), // LEFT (0)
        float3(1, 0, 0),  // RIGHT (1)
        float3(0, -1, 0), // BOTTOM (2)
        float3(0, 1, 0),  // TOP (3)
        float3(0, 0, -1), // NEAR (4)
        float3(0, 0, 1)   // FAR (5)
    };

    return normals[face];
}
```

### 4.4. UV 복원: getVoxelTexcoord() 함수 (BasicVS.hlsl)

```hlsl
float2 getVoxelTexcoord(float3 position, uint face) {
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

## 5. 문제점 & 해결

### 5.1. 텍스쳐 좌표 넘침

**문제**: MSAA로 인해 Interpolation 방식에 따라 Texture 좌표가 넘치고 frac 연산으로 정확한 텍스쳐 좌표를 구할 수 없었음

**해결**: `sample` 지정자 사용하여 렌더 비용과 트레이드오프

### 5.2. AO 부재

**문제**: 다른 Voxel Engine에서는 대부분 AO 정도를 주변 블록 위치에 따라 정적으로 넣어줬고 내 코드는 AO 검사를 진행하지 않음

**해결**: Deferred Pass 단계에서 SSAO 진행

### 5.3. CPU -> GPU 대역폭을 확실히 줄였는가

**문제**: 정점 데이터를 36byte를 4byte로 크게 줄였으나, 기본적으로 정점 개수 자체가 많은 경우가 존재

**해결**: 정점 자체를 줄이는 Greedy Meshing 진행

## 6. 결과

### 6.1. 메모리 절약 효과

**정점당 메모리 비교**:

| 항목              | 전통적 방식   | Binary 방식  | 절약률    |
| ----------------- | ------------- | ------------ | --------- |
| Position (float3) | 12 바이트     | -            | -         |
| Normal (float3)   | 12 바이트     | -            | -         |
| Texcoord (float2) | 8 바이트      | -            | -         |
| TexIndex (uint)   | 4 바이트      | -            | -         |
| **압축 데이터**   | -             | 4 바이트     | -         |
| **총 크기**       | **36 바이트** | **4 바이트** | **88.9%** |

**메모리 대역폭 절약**:

- 60 FPS, 100만 정점/프레임
- 전통적: 36 MB/frame × 60 = **2,160 MB/s**
- Binary: 4 MB/frame × 60 = **240 MB/s**
- **절약: 1,920 MB/s (89%)**

## 7. 회고

- 정점 데이터의 크기 자체를 크게 줄일 수 있어서 효과적이였음
- 줄인 것은 만족스러우나, 거기에 따른 다양한 트레이드 오프가 존재했음
  - 텍스쳐 인덱스가 더욱 더 많아지면 어떻게 할 것인가?, Normal의 방향이 6면이 아닌 회전을 하는 경우 어떻게 할 것인가?
- 비용절감에 따른 로직이 다양한 환경에 대한 유연성이 부족하다는 생각을 하게 되었음
