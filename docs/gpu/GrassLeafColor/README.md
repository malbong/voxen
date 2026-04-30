# Grass & Leaf Color — Climate Noise 기반 색상 결정

<img width="800" height="460" alt="Image" src="https://github.com/user-attachments/assets/2b7358cc-a458-4c6f-a442-86ff56a54b58" />

<img width="800" height="460" alt="Image" src="https://github.com/user-attachments/assets/065010ce-490e-4d36-b182-420877811034" />

<img width="800" height="460" alt="Image" src="https://github.com/user-attachments/assets/bce83e8f-e163-44ee-94ee-81aac305da19" />

## 1. 개요

잔디(Grass)와 잎사귀(Foliage) 블록의 색상을 바이옴별 고정 색상이 아닌, **연속적인 기후 노이즈(Temperature, Humidity)** 를 통해 결정하는 시스템이다.
CPU에서 기후 노이즈를 텍스처로 구워 GPU에 전달하고, Pixel Shader에서 이 텍스처를 ColorMap의 UV 좌표로 사용하여 최종 색상을 곱한다.

## 2. 도입 동기

바이옴마다 잔디/잎 색상을 하나의 고정값으로 지정하면, 바이옴 경계면에서 색상이 자연스럽지 못하다.
Plains의 초록과 Savanna의 갈색이 한 블록 차이로 뚝 바뀌는 것은 시각적으로 매우 부자연스럽다.

따라서 색상 결정을 바이옴 판정과 분리하고, **바이옴 분류에 사용되는 연속적 노이즈 값 자체**를 색상 참조에 사용하는 접근을 택했다.

## 3. 핵심 아이디어

Grass & Leaf Color Map을 Temperature, Humidity 노이즈 값에 따른 LUT를 구성한다.
쉐이딩 시에 해당 블록의 Temperature, Humidity를 구하고, 그 값을 이용하여 Color Map에서 샘플링하여 Albedo를 구성한다.

이 때, Temperature, Humidity 값은 Block의 별도의 데이터가 아니라 Pixel Shader에 전달될 수 없다.
climateNoiseMap(Temperature, Humidity)을 카메라 이동에 맞춰 동적으로 텍스쳐를 업데이트하여 GPU에 전달한다.
BasicPS에서 World Position을 climateNoiseMap에 투영하고 샘플링하여 Grass & Leaf ColorMap을 샘플링하게 된다.

## 4. 구현 내용

### 4.1 CPU: Climate Noise 텍스처 생성 (WorldMap.cpp)

Climate 텍스처는 카메라를 중심으로 한 정사각형 영역을 1블록 = 1텍셀 해상도로 기록한다.

| 상수                               | 값                                 | 의미                          |
| ---------------------------------- | ---------------------------------- | ----------------------------- |
| `CLIMATE_MAP_BUFFER_SIZE`          | `CHUNK_COUNT × CHUNK_SIZE` (= 672) | 텍스처 해상도 (672×672)       |
| `CLIMATE_MAP_WORLD_SIZE_PER_PIXEL` | 1                                  | 1텍셀 = 1블록                 |
| 텍스처 포맷                        | `R32G32_FLOAT`                     | R = Temperature, G = Humidity |

`GetClimateNoise()`에서 각 텍셀의 월드 좌표를 계산하고, `Terrain::GetTemperature/GetHumidity`를 호출한다.

```cpp
// climate Buffer 초기화
// WorldMap::Initialize(cameraPosition)
m_climateMapData.resize(CLIMATE_MAP_BUFFER_SIZE * CLIMATE_MAP_BUFFER_SIZE);
m_climateMapOffsetPosition =
        Utils::CalcOffsetPos(cameraPosition, CLIMATE_MAP_WORLD_SIZE_PER_PIXEL);
for (int z = 0; z < CLIMATE_MAP_BUFFER_SIZE; ++z) {
        for (int x = 0; x < CLIMATE_MAP_BUFFER_SIZE; ++x) {
                m_climateMapData[x + z * CLIMATE_MAP_BUFFER_SIZE] = GetClimateNoise(x, z);
        }
}
if (!DXUtils::UpdateTexture2DBuffer(Graphics::climateMapBuffer, m_climateMapData,
                CLIMATE_MAP_BUFFER_SIZE, CLIMATE_MAP_BUFFER_SIZE)) {
        return false;
}

...
// WorldMap::GetClimateNoise(int x, int z)
int worldX = (int)m_climateMapOffsetPosition.x - (CLIMATE_MAP_WORLD_SIZE / 2)
                + CLIMATE_MAP_WORLD_SIZE_PER_PIXEL * x;
int worldZ = (int)m_climateMapOffsetPosition.z + (CLIMATE_MAP_WORLD_SIZE / 2)
                - CLIMATE_MAP_WORLD_SIZE_PER_PIXEL * z;

float temperature = Terrain::GetTemperature(worldX, worldZ);
float humidity    = Terrain::GetHumidity(worldX, worldZ);

return CLIMATE(temperature, humidity);  // {float t, float h}
```

### 4.2 Climate Buffer 업데이트

텍스처는 카메라 중심으로 생성되므로, 카메라 이동 시 전체를 재생성하면 비효율적이다.
그나마 조금 더 효율적인 **Shift-and-Fill** 방식을 사용한다.

1. 카메라가 1블록 이동하면, 기존 데이터를 이동 방향의 반대로 1행/열 밀기 (`ShiftClimateMapData`)
2. 새로 드러난 가장자리 1행/열만 노이즈를 재계산 (`UpdateClimateMapData`)
3. `DXUtils::UpdateTexture2DBuffer`로 GPU에 업로드

```cpp
Vector3 newOffsetPosition = Utils::CalcOffsetPos(cameraPosition, CLIMATE_MAP_WORLD_SIZE_PER_PIXEL);
Vector3 offsetDiff = m_climateMapOffsetPosition - newOffsetPosition;
if (offsetDiff.Length() > 0) {
        m_climateMapOffsetPosition = newOffsetPosition;

        int dx = Utils::Signf(offsetDiff.x);
        int dz = -Utils::Signf(offsetDiff.z); // z 방향은 반대

        ShiftClimateMapData(dx, dz); // 단순한 격자 이동
        UpdateClimateMapData(dx, dz); // 마지막 빈 행/열 채우기

        DXUtils::UpdateTexture2DBuffer(Graphics::climateMapBuffer, m_climateMapData,
                CLIMATE_MAP_BUFFER_SIZE, CLIMATE_MAP_BUFFER_SIZE);
}
```

### 4.3 PS: Climate Noise 샘플링과 ColorMap 참조 (BasicPS.hlsl)

`getAlbedo()` 함수에서 잔디/잎 블록일 때 climate noise 텍스처를 샘플링하고, 그 값을 ColorMap의 UV로 사용한다.

#### 텍스처 바인딩

```hlsl
Texture2D grassColorMap   : register(t3);  // 잔디 색상 LUT
Texture2D foliageColorMap : register(t4);  // 잎사귀 색상 LUT
Texture2D climateNoiseMap : register(t5);  // Climate(T, H) 텍스처
```

Climate Noise 텍스쳐

<table>
  <tr>
    <td><img width="400" height="400" alt="Image" src="https://github.com/user-attachments/assets/c70add2f-5d74-402c-9f10-60beca44da4e" /></td>
    <td><img width="400" height="400" alt="Image" src="https://github.com/user-attachments/assets/312add01-b06a-4c01-be4a-d364bd03345e" /></td>
    <td><img width="400" height="400" alt="Image" src="https://github.com/user-attachments/assets/5afbeed2-5482-4585-859a-a84d890a855f" /></td>
  </tr>
</table>

#### 대상 블록 판정

Block Data에 블록의 타입이 쉐이더 내부에서 `texIndex`로 구분되기에 `texIndex`로 잔디 계열과 잎사귀 계열을 분류한다.

```hlsl
bool useGrassColor(uint texIndex)
{
    return texIndex <= 2 || texIndex == 128 || texIndex == 131 ||
           texIndex == 148 || texIndex == 153 || ...;
}
```

#### Climate Noise 샘플링 좌표 계산

```hlsl
float3 faceBiasPos = -normal * 1e-4;
float2 diffOffsetPos = floor(worldPos.xz + faceBiasPos.xz) - floor(eyePos.xz);

float texelSize = 1.0 / (CHUNK_COUNT * CHUNK_SIZE);     // 1.0 / (Climate Noises Texture 크기)
float2 climateTexcoord = float2(
    0.5 + diffOffsetPos.x * texelSize,
    0.5 - diffOffsetPos.y * texelSize
);
climateTexcoord += float2(texelSize * 0.5, texelSize * 0.5);
```

이 좌표 계산 과정이 이 시스템의 핵심이다. 단계별로 설명하면:

1. **Face Bias**
   `faceBiasPos = -normal * 1e-4`. 블록의 면 위 위치(`worldPos`)를 법선 반대 방향으로 미세하게 밀어 블록 내부로 수축시킨다.
   이는 인접 블록 경계면에서 `floor()`가 의도와 다른 블록 좌표를 반환하는 문제를 방지한다.
   예를 들어 `worldPos.x = 5.0`인 +X 면의 경우, bias 없이 `floor(5.0) = 5`가 되어 옆 블록을 참조하게 된다.

2. **카메라 상대 좌표**
   `floor(worldPos.xz) - floor(eyePos.xz)`. climate 텍스처가 카메라 중심이므로, 월드 위치를 카메라 기준 상대 블록 좌표로 변환한다.

3. **UV 변환**
   상대 좌표에 `texelSize`를 곱해 [0, 1] UV 공간으로 변환한다.
   카메라 위치가 UV `(0.5, 0.5)`에 대응한다. Y축(Z 월드축)은 텍스처 좌표계가 반대이므로 부호를 뒤집는다.

4. **Half-texel Offset**
   `climateTexcoord += float2(texelSize * 0.5, texelSize * 0.5)`.
   Point Sampling에서 텍셀 중심을 정확히 찍기 위한 보정이다. 이 오프셋이 없으면, 정확히 텍셀 경계에 놓인 좌표가 인접 텍셀을 샘플링할 수 있다.

#### ColorMap 참조와 최종 색상 적용

```hlsl
float2 th = climateNoiseMap.SampleLevel(pointClampSS, climateTexcoord, 0.0).rg;

float3 climateColor = float3(0.0, 0.0, 0.0);
if (useGrassColor(texIndex))
    climateColor = grassColorMap.SampleLevel(pointClampSS, float2(th.x, 1.0 - th.y), 0.0).rgb;
if (useFoliageColor(texIndex))
    climateColor = foliageColorMap.SampleLevel(pointClampSS, float2(th.x, 1.0 - th.y), 0.0).rgb;

albedo.rgb *= climateColor;
```

### 4.4 전체 흐름

```
[CPU]
Terrain::GetTemperature/GetHumidity (Perlin FBM, [0,1])
        ↓
WorldMap::GetClimateNoise → CLIMATE{t, h}
        ↓
Update climateMapBuffer (R32G32_FLOAT, 672×672)

        ↓ GPU Upload

[GPU - BasicPS.hlsl::getAlbedo()]
worldPos → 카메라 상대 좌표 → climateTexcoord (UV)
        ↓
climateNoiseMap.Sample → float2(temperature, humidity)
        ↓
grassColorMap / foliageColorMap.Sample(UV = (t, 1-h))
        ↓
albedo.rgb *= climateColor
```

## 5. 문제점 & 해결

### 5.1 블록 경계에서의 잘못된 텍셀 참조

**문제**: 블록 면의 월드 좌표가 정확히 정수 경계에 놓이면, `floor()` 결과가 의도한 블록이 아닌 인접 블록을 가리킬 수 있다. 예를 들어 X=5.0에 위치한 +X 면은 `floor(5.0) = 5`가 되어 자기 블록(X=4~5)이 아닌 옆 블록(X=5~6)의 climate을 참조한다.

**해결**: `faceBiasPos = -normal * 1e-4`로 법선 반대 방향(블록 내부)으로 미세하게 수축한 뒤 `floor()`를 적용한다. 5.0 → 4.9999로 보정되어 올바른 블록 좌표가 된다.

### 5.2 Point Sampling 시 텍셀 중심 누락

**문제**: `diffOffsetPos`가 0일 때(카메라 위치 블록) UV가 정확히 0.5가 되는데, 672×672 텍스처에서 이 좌표가 텍셀 경계에 놓여 부정확한 샘플링이 발생할 수 있다.

**해결**: Half-texel offset(`texelSize * 0.5`)을 더해 항상 텍셀 중심을 샘플링하도록 한다. 이는 Point Sampling에서 흔히 사용되는 표준적인 보정 기법이다.

## 6. 회고

- GPU에 전달되는 현재 픽셀의 데이터 정보가 적기에 어떻게하면 동적으로 색을 결정할 수 있을까에 대한 고민이 많았음
- World Position을 노이즈 텍스쳐의 UV로 투영하고, 샘플링하여 Temperature, Humidity를 얻고 그것을 LUT로 Color Map으로 색을 결정하는 방식은 좋았음
- 자연스러운 변화가 Minecraft와 매우 유사하여 결과는 만족하나, 카메라 이동에 따른 노이즈 버퍼의 Update와 노이즈 호출 함수는 계속 신경쓰임
