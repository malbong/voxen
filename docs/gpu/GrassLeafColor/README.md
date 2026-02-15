# Grass & Leaf Color — Climate Noise 기반 색상 결정

## 1. 개요

잔디(Grass)와 잎사귀(Foliage) 블록의 색상을 바이옴별 고정 색상이 아닌, **연속적인 기후 노이즈(Temperature, Humidity)** 를 통해 결정하는 시스템이다. CPU에서 기후 노이즈를 텍스처로 구워 GPU에 전달하고, Pixel Shader에서 이 텍스처를 ColorMap의 UV 좌표로 사용하여 최종 색상을 곱한다.

## 2. 도입 동기

바이옴마다 잔디/잎 색상을 하나의 고정값으로 지정하면, 바이옴 경계면에서 색상이 불연속적으로 끊긴다. Plains의 초록과 Desert의 갈색이 한 블록 차이로 뚝 바뀌는 것은 시각적으로 매우 부자연스럽다.

이 문제의 근본 원인은 **이산적(discrete) 바이옴 분류**에 색상이 직접 종속되기 때문이다. 바이옴 판정 자체가 경계를 갖는 한, 색상도 경계를 가질 수밖에 없다. 따라서 색상 결정을 바이옴 판정과 분리하고, **바이옴 분류에 사용되는 연속적 노이즈 값 자체**를 색상 참조에 사용하는 접근을 택했다.

## 3. 핵심 아이디어

바이옴 분류에 사용되는 Temperature/Humidity 노이즈는 Perlin FBM으로 생성되어 공간적으로 연속이다. 이 연속 값을 2D ColorMap 텍스처의 UV 좌표로 직접 사용하면, 색상 전환이 노이즈의 연속성을 그대로 따라가게 된다.

```
[Temperature, Humidity]  →  ColorMap UV  →  색상
     (연속 노이즈)           (연속 좌표)      (자연스러운 그라데이션)
```

핵심은 **바이옴 분류를 거치지 않고 노이즈 → 색상으로 직행**하는 것이다. 같은 바이옴 안에서도 노이즈 값의 미세한 차이가 색상 변화를 만들고, 경계면에서도 노이즈가 연속이므로 색상이 부드럽게 전환된다.

## 4. 구현 내용

### 4.1 CPU: Climate Noise 텍스처 생성 (WorldMap.cpp)

Climate 텍스처는 카메라를 중심으로 한 정사각형 영역을 1블록 = 1텍셀 해상도로 기록한다.

| 상수 | 값 | 의미 |
|---|---|---|
| `CLIMATE_MAP_BUFFER_SIZE` | `CHUNK_COUNT × CHUNK_SIZE` (= 672) | 텍스처 해상도 (672×672) |
| `CLIMATE_MAP_WORLD_SIZE_PER_PIXEL` | 1 | 1텍셀 = 1블록 |
| 텍스처 포맷 | `R32G32_FLOAT` | R = Temperature, G = Humidity |

`GetClimateNoise()`에서 각 텍셀의 월드 좌표를 계산하고, `Terrain::GetTemperature/GetHumidity`를 호출한다.

```cpp
// WorldMap.cpp:229
CLIMATE WorldMap::GetClimateNoise(int x, int z)
{
    int worldX = (int)m_climateMapOffsetPosition.x - (CLIMATE_MAP_WORLD_SIZE / 2)
                 + CLIMATE_MAP_WORLD_SIZE_PER_PIXEL * x;
    int worldZ = (int)m_climateMapOffsetPosition.z + (CLIMATE_MAP_WORLD_SIZE / 2)
                 - CLIMATE_MAP_WORLD_SIZE_PER_PIXEL * z;

    float temperature = Terrain::GetTemperature(worldX, worldZ);
    float humidity    = Terrain::GetHumidity(worldX, worldZ);

    return CLIMATE(temperature, humidity);  // {float t, float h}
}
```

Temperature와 Humidity는 각각 독립적인 Perlin FBM이다.

| 파라미터 | Temperature | Humidity |
|---|---|---|
| Scale | 5024 | 5048 |
| Seed | 653 | 157 |
| Octaves | 5 | 5 |
| 출력 범위 | [0, 1] | [0, 1] |

노이즈 결과를 `clamp(noise * 2, -1, 1)` 후 `(v + 1) * 0.5`로 [0, 1]에 매핑한다. 곱셈 2배로 중간 영역의 contrast를 높여 색상 변화가 뚜렷해진다.

### 4.2 Climate 텍스처 스크롤링

텍스처는 카메라 중심으로 생성되므로, 카메라 이동 시 전체를 재생성하면 비효율적이다. 대신 **Shift-and-Fill** 방식을 사용한다.

1. 카메라가 1블록 이동하면, 기존 데이터를 이동 방향의 반대로 1행/열 밀기 (`ShiftClimateMapData`)
2. 새로 드러난 가장자리 1행/열만 노이즈를 재계산 (`UpdateClimateMapData`)
3. `DXUtils::UpdateTexture2DBuffer`로 GPU에 업로드

이동량이 1텍셀이므로 매 프레임 최대 1행 + 1열만 갱신하면 된다.

### 4.3 PS: Climate Noise 샘플링과 ColorMap 참조 (BasicPS.hlsl)

`getAlbedo()` 함수에서 잔디/잎 블록일 때 climate noise 텍스처를 샘플링하고, 그 값을 ColorMap의 UV로 사용한다.

#### 텍스처 바인딩

```hlsl
Texture2D grassColorMap   : register(t3);  // 잔디 색상 LUT
Texture2D foliageColorMap : register(t4);  // 잎사귀 색상 LUT
Texture2D climateNoiseMap : register(t5);  // Climate(T, H) 텍스처
```

#### 대상 블록 판정

```hlsl
bool useGrassColor(uint texIndex)
{
    return texIndex <= 2 || texIndex == 128 || texIndex == 131 ||
           texIndex == 148 || texIndex == 153 || ...;
}

bool useFoliageColor(uint texIndex)
{
    return (64 <= texIndex && texIndex <= 70);
}
```

텍스처 인덱스로 잔디 계열과 잎사귀 계열을 분류한다. 잔디 계열은 `grassColorMap`, 잎사귀 계열은 `foliageColorMap`을 참조한다.

#### Climate Noise 샘플링 좌표 계산

```hlsl
float3 faceBiasPos = -normal * 1e-4;
float2 diffOffsetPos = floor(worldPos.xz + faceBiasPos.xz) - floor(eyePos.xz);

float texelSize = 1.0 / (CHUNK_COUNT * CHUNK_SIZE);
float2 climateTexcoord = float2(
    0.5 + diffOffsetPos.x * texelSize,
    0.5 - diffOffsetPos.y * texelSize
);
climateTexcoord += float2(texelSize * 0.5, texelSize * 0.5);
```

이 좌표 계산 과정이 이 시스템의 핵심이다. 단계별로 설명하면:

1. **Face Bias** — `faceBiasPos = -normal * 1e-4`. 블록의 면 위 위치(`worldPos`)를 법선 반대 방향으로 미세하게 밀어 블록 내부로 수축시킨다. 이는 인접 블록 경계면에서 `floor()`가 의도와 다른 블록 좌표를 반환하는 문제를 방지한다. 예를 들어 `worldPos.x = 5.0`인 +X 면의 경우, bias 없이 `floor(5.0) = 5`가 되어 옆 블록을 참조하게 된다.

2. **카메라 상대 좌표** — `floor(worldPos.xz) - floor(eyePos.xz)`. climate 텍스처가 카메라 중심이므로, 월드 위치를 카메라 기준 상대 블록 좌표로 변환한다.

3. **UV 변환** — 상대 좌표에 `texelSize`를 곱해 [0, 1] UV 공간으로 변환한다. 카메라 위치가 UV `(0.5, 0.5)`에 대응한다. Y축(Z 월드축)은 텍스처 좌표계가 반대이므로 부호를 뒤집는다.

4. **Half-texel Offset** — `+= texelSize * 0.5`. Point Sampling에서 텍셀 중심을 정확히 찍기 위한 보정이다. 이 오프셋이 없으면, 정확히 텍셀 경계에 놓인 좌표가 인접 텍셀을 샘플링할 수 있다.

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

- `th.r`(Temperature)이 ColorMap의 U, `1.0 - th.g`(Humidity 반전)이 V가 된다
- ColorMap은 2D LUT(Lookup Table) 이미지로, 온도-습도 조합에 따른 색상이 미리 정의되어 있다
- `pointClampSS`로 샘플링하여 블록 단위로 이산적인 색상을 보장한다
- 최종적으로 `albedo.rgb *= climateColor`로 원래 텍스처 색상에 기후 색상을 곱한다 — Multiply 블렌딩이므로 원본 텍스처의 명암 디테일이 유지된다

### 4.4 전체 파이프라인 요약

```
[CPU]
Terrain::GetTemperature/GetHumidity (Perlin FBM, [0,1])
        ↓
WorldMap::GetClimateNoise → CLIMATE{t, h}
        ↓
climateMapBuffer (R32G32_FLOAT, 672×672)
        ↓ GPU Upload

[GPU - BasicPS.hlsl::getAlbedo()]
worldPos → 카메라 상대 좌표 → climateTexcoord (UV)
        ↓
climateNoiseMap.Sample → float2(temperature, humidity)
        ↓
grassColorMap / foliageColorMap.Sample(UV = (t, 1-h))
        ↓
albedo.rgb *= climateColor  (Multiply 블렌딩)
```

## 5. 문제점 & 해결

### 5.1 블록 경계에서의 잘못된 텍셀 참조

**문제**: 블록 면의 월드 좌표가 정확히 정수 경계에 놓이면, `floor()` 결과가 의도한 블록이 아닌 인접 블록을 가리킬 수 있다. 예를 들어 X=5.0에 위치한 +X 면은 `floor(5.0) = 5`가 되어 자기 블록(X=4~5)이 아닌 옆 블록(X=5~6)의 climate을 참조한다.

**해결**: `faceBiasPos = -normal * 1e-4`로 법선 반대 방향(블록 내부)으로 미세하게 수축한 뒤 `floor()`를 적용한다. 5.0 → 4.9999로 보정되어 올바른 블록 좌표가 된다.

### 5.2 Point Sampling 시 텍셀 중심 누락

**문제**: `diffOffsetPos`가 0일 때(카메라 위치 블록) UV가 정확히 0.5가 되는데, 672×672 텍스처에서 이 좌표가 텍셀 경계에 놓여 부정확한 샘플링이 발생할 수 있다.

**해결**: Half-texel offset(`texelSize * 0.5`)을 더해 항상 텍셀 중심을 샘플링하도록 한다. 이는 Point Sampling에서 흔히 사용되는 표준적인 보정 기법이다.

### 5.3 트레이드오프 — Point vs Linear Sampling

climate 텍스처와 ColorMap 모두 `pointClampSS`(Point Sampling)을 사용한다. Linear Sampling을 사용하면 블록 간 보간이 되어 더 부드러운 그라데이션이 가능하지만, 복셀 아트 스타일에서는 블록 단위의 균일한 색상이 더 적합하다. 노이즈 자체의 연속성이 충분히 완만한 전환을 제공하므로, 블록 단위 이산 색상으로도 자연스러운 결과를 얻는다.

## 6. 결과

- 바이옴 경계면에서 색상이 한 블록 단위로 끊기지 않고, 수십 블록에 걸쳐 점진적으로 전환된다
- 같은 바이옴 내에서도 미세한 색조 변화가 있어, 단조로운 느낌이 줄어든다
- Climate 텍스처가 카메라와 함께 스크롤되므로, 시야 안의 모든 블록이 항상 올바른 색상을 가진다
- Multiply 블렌딩으로 원본 텍스처의 디테일(명암, 패턴)을 유지하면서 기후별 색조만 변경한다

## 7. 회고

- ColorMap이 외부 이미지(`grass_colormap.png`, `foliage_colormap.png`)에 종속되어, 색상 팔레트를 조정하려면 이미지를 직접 편집해야 한다. 런타임에서 ColorMap을 절차적으로 생성하거나 파라미터화할 수 있으면 더 유연할 것이다
- 현재 Climate 텍스처 해상도가 1블록 = 1텍셀이다. 이는 정확하지만, 만약 렌더 거리가 늘어나면 텍스처 크기도 비례하여 증가한다. LOD에 따라 먼 거리에서는 저해상도 climate 텍스처를 사용하는 방식도 고려할 수 있다
- Point Sampling 대신 ColorMap에만 Linear Sampling을 적용하면, 블록 간 색상이 미세하게 블렌딩되는 효과를 추가로 줄 수 있다. 복셀 스타일과의 균형을 찾을 수 있는 부분이다
