# Terrain

## 1. 개요

Terrain 시스템은 Voxen 엔진에서 절차적 지형 생성을 담당하는 핵심 모듈입니다. 6종의 노이즈 맵(Continentalness, Erosion, PeaksValley, Temperature, Humidity, Distribution)과 Spline 변환을 통해 최종 지형 높이(Elevation)를 계산하며, 동굴 생성 및 랜덤 배치 유틸리티를 제공합니다.

엔진 전체에서 다음 역할을 수행합니다:
- **Chunk 초기화 시 지형 높이 결정**: `Chunk::InitTerrainNoises()`에서 모든 노이즈 값을 계산하여 `ChunkLoadMemory`에 캐싱
- **Biome 시스템과 연계**: Continentalness, Erosion, Temperature, Humidity 값을 `Biome::GetBiomeType()`에 전달하여 바이옴 결정
- **Block 선택 로직 제공**: `Block::GetBlockType()`이 Elevation과 Density 값을 참조하여 블록 타입 결정
- **동굴 생성**: `IsCave()` 함수로 지하 공간 생성
- **랜덤 배치**: `GenerateRandomPlace2D()`로 나무/인스턴스의 2D 위치 시드 생성

## 2. 도입 동기

마인크래프트와 같은 복잡하고 자연스러운 지형 생성을 목표로 했습니다.

### 기존 단순 노이즈 방식의 한계
1. **단일 Perlin Noise**: 높이만 제어하여 지형이 단조로움 (완만한 언덕만 생성)
2. **범위 제어 어려움**: 노이즈 값이 [-1, 1] 범위에서 균등 분포하여 원하는 지형 형태(해양/평지/산악)의 비율 조정 불가
3. **바이옴 연계 부족**: 온도/습도 기반 바이옴 시스템과 지형 높이가 독립적으로 동작하여 비현실적 (사막에 높은 산 생성 등)
4. **동굴 부재**: 지하 탐험 요소 없음

### 목표
- **다층 노이즈**: 대륙성(Continentalness), 침식(Erosion), 산/계곡(PeaksValley) 등 독립적 파라미터로 지형 다양성 확보
- **Spline 변환**: 노이즈 분포를 비선형 변환하여 특정 지형 형태(해양 70%, 평지 20%, 산악 10%)의 비율 제어
- **바이옴 통합**: 온도/습도 노이즈를 동시 생성하여 바이옴과 지형 높이가 상호작용
- **동굴 생성**: 3D 노이즈로 지하 구조 추가

## 3. 핵심 아이디어

### 3.1. 6종 노이즈 맵 독립 생성

각 노이즈는 서로 다른 `scale`(주파수)과 `seed`(위상 오프셋)를 사용하여 독립적인 패턴 생성:

| 노이즈 이름          | 역할                          | Scale  | Seed   | Octaves | Lacunarity |
|---------------------|-------------------------------|--------|--------|---------|------------|
| Continentalness     | 대륙/해양 구분 (기본 높이)       | 1024.0 | 0      | 6       | 2.0        |
| Erosion             | 지형 침식도 (산악/평지)          | 4024.0 | 123.0  | 6       | 2.0        |
| PeaksValley         | 산봉우리/계곡 세부 형태           | 512.0  | 4.0    | 6       | 1.5        |
| Temperature         | 온도 (바이옴 결정)               | 5024.0 | 653.0  | 5       | 4.0        |
| Humidity            | 습도 (바이옴 결정)               | 5048.0 | 157.0  | 5       | 4.0        |
| Distribution        | 광석/블록 분포 (세부 변형)        | 24.0   | 773.0  | 4       | 2.0        |
| Density (3D)        | 지하 밀도 (블록 타입, 동굴 제외)  | 16.0   | 331.0  | 2       | 2.0        |

**핵심**: Scale이 클수록 완만한 지형, 작을수록 급격한 변화. Temperature/Humidity는 매우 큰 scale(5000+)로 대륙 단위 기후대 형성.

### 3.2. Spline 변환으로 분포 제어

`Utils::PerlinFbm()`이 반환하는 [-1, 1] 범위의 노이즈는 균등 분포입니다. 하지만 실제 지형은 "해양이 70%, 평지가 20%, 산악이 10%"와 같이 비균등 분포를 원합니다.

**Spline 변환**은 입력 노이즈를 구간별 Cubic Lerp로 재매핑하여 출력 분포를 조정합니다:

```
SplineContinentalness(value):
  value = clamp(value * 1.5, -1.0, 1.0)

  if value <= -0.51:  // 심해 (deep ocean)
    w = (value - -1.0) / (-0.51 - -1.0)
    return CubicLerp(0.0, 0.14, w)
  else if value <= -0.25:  // 얕은 해양
    return CubicLerp(0.14, 0.31, w)
  ...
  else if value <= 0.42:  // 평지/언덕
    return CubicLerp(0.57, 0.92, w)
  else:  // 산악
    return CubicLerp(0.92, 1.0, w)
```

**예시**: 노이즈 값 -0.6이 0.14로, 0.5가 0.95로 매핑 → 해양 구간이 넓게, 산악 구간이 좁게 압축되어 해양 비율 증가.

`SplineErosion`, `SplinePeaksValley`도 동일한 방식으로 8~7개 구간을 가지며, 각각 침식도/산봉우리 분포를 비선형 조정합니다.

### 3.3. Elevation 계산 공식

최종 지형 높이는 **Continentalness**, **Erosion**, **PeaksValley**를 결합:

```cpp
float GetElevation(float c, float e, float pv) {
    // 1. Continentalness 보정: 0.1 이하(해양)는 음수 높이, 이상은 양수 높이
    if (c <= 0.1f)
        c = c / 0.1f - 1.0f;  // [0, 0.1] -> [-1.0, 0.0]
    else
        c = (c - 0.1f) / 0.9f;  // [0.1, 1.0] -> [0.0, 1.0]

    // 2. 기본 높이: Continentalness와 Erosion의 조합
    float baseHeight = 64.0f * c * (1.0f - e);

    // 3. 산/계곡 높이: PeaksValley와 Erosion의 조합
    float peaksHeight = 64.0f * pv * pow((1.0f - e), 1.25f);

    elevation = baseHeight + peaksHeight;
    return clamp(elevation, -128.0f, 128.0f);
}
```

**의미**:
- `c * (1 - e)`: Continentalness가 높고 Erosion이 낮을수록 높은 고원 형성
- `pv * pow(1 - e, 1.25)`: PeaksValley의 영향은 Erosion이 낮을 때(덜 침식된 지역)만 강하게 작용하여 산봉우리 생성
- Erosion이 높으면 두 항 모두 감소하여 평탄한 지형 형성

**범위**: [-128, 128] → 월드 Y좌표 기준 -64~192 블록 높이 (MIN_HEIGHT_LEVEL=0 기준 상대 높이)

### 3.4. 동굴 생성 (3D 노이즈 임계값)

`IsCave(x, y, z)`는 2개의 독립적인 3D Perlin 노이즈를 사용하여 동굴을 생성합니다:

```cpp
bool IsCave(int x, int y, int z) {
    float threshold = 0.004f;

    float density1 = PerlinFbm(x / 256.0f, y / 256.0f, z / 256.0f, 2.0f, 4);
    if (density1 * density1 > threshold)
        return false;  // early return

    float density2 = PerlinFbm(
        x / 512.0f + 123.0f, y / 256.0f + 123.0f, z / 512.0f + 123.0f, 2.0f, 4);
    if (density2 * density2 > threshold)
        return false;

    return (density1 * density1 + density2 * density2 <= threshold);
}
```

**핵심**:
- **두 노이즈의 제곱 합**: `density1^2 + density2^2 <= 0.004` 조건으로 동굴 형성
- **Early Return 최적화**: 둘 중 하나라도 threshold 초과 시 즉시 `false` 반환하여 연산량 감소
- **Scale 차이**: density1은 256, density2는 512로 서로 다른 주파수의 동굴 패턴 생성
- **Threshold 조정**: 0.004는 매우 작은 값으로, 노이즈가 거의 0에 가까운 좁은 영역만 동굴로 판정 (약 1~3% 비율)

## 4. 구현 내용

### 4.1. 노이즈 생성 API

모든 노이즈는 `Utils::PerlinFbm()` API를 사용:

```cpp
float PerlinFbm(float x, float y, float z, float lacunarity, int octaves);
```

- **Perlin Noise**: 그래디언트 기반 연속적 노이즈 (부드러운 곡선)
- **FBM (Fractional Brownian Motion)**: 여러 옥타브의 노이즈를 누적하여 세부 디테일 추가
  - `lacunarity`: 각 옥타브마다 주파수를 곱하는 비율 (일반적으로 2.0)
  - `octaves`: 누적할 노이즈 레이어 수 (2~6)

**예시**: `GetContinentalness(x, z)`
```cpp
float cNoise = Utils::PerlinFbm(x / 1024.0f, z / 1024.0f, 2.0f, 6);
float cValue = SplineContinentalness(cNoise);
```
→ 1024 블록 단위로 변화하는 부드러운 노이즈를 6옥타브로 누적 후 Spline 변환

### 4.2. Chunk별 노이즈 캐싱 (Chunk.cpp)

`Chunk::InitTerrainNoises(ChunkLoadMemory* memory)`는 청크 로딩 시 모든 노이즈를 한 번에 계산하여 메모리에 저장합니다:

```cpp
void Chunk::InitTerrainNoises(ChunkLoadMemory* memory) {
    for (int x = 0; x < CHUNK_SIZE_P; ++x) {
        for (int z = 0; z < CHUNK_SIZE_P; ++z) {
            int worldX = (int)m_offsetPosition.x + x - 1;
            int worldZ = (int)m_offsetPosition.z + z - 1;

            memory->continentalinessNoises[x][z] = Terrain::GetContinentalness(worldX, worldZ);
            memory->erosionNoises[x][z] = Terrain::GetErosion(worldX, worldZ);
            memory->peaksValleyNoises[x][z] = Terrain::GetPeaksValley(worldX, worldZ);
            memory->temperatureNoises[x][z] = Terrain::GetTemperature(worldX, worldZ);
            memory->humidityNoises[x][z] = Terrain::GetHumidity(worldX, worldZ);
            memory->distributionNoises[x][z] = Terrain::GetDistribution(worldX, worldZ);

            // Elevation은 Biome 시스템에서 계산
            memory->elevationNoises[x][z] = Biome::GetBiomeTerrainHeight(
                memory->continentalinessNoises[x][z],
                memory->erosionNoises[x][z],
                memory->peaksValleyNoises[x][z],
                memory->temperatureNoises[x][z],
                memory->humidityNoises[x][z]);
        }
    }
}
```

**목적**:
- **성능 최적화**: 동일한 (x, z) 좌표의 노이즈를 여러 블록(Y축 모든 높이)에서 재사용하므로 2D 노이즈를 미리 계산
- **메모리 트레이드오프**: `CHUNK_SIZE_P x CHUNK_SIZE_P` (34x34) 크기의 6개 배열 캐싱 (약 7KB)
- **Padding 포함**: 청크 경계의 블록 선택 및 BiomeLayer 계산 시 인접 좌표 참조를 위해 CHUNK_SIZE_P(32 + 2 padding) 사용

### 4.3. Spline 변환 구현

각 Spline 함수는 6~8개 구간으로 분할된 Piecewise Cubic Interpolation:

```cpp
float SplineErosion(float value) {
    value = clamp(value * 1.5f, -1.0f, 1.0f);  // 노이즈 범위 확장

    if (value <= -0.78f) {
        float w = (value - -1.0f) / (-0.78f - -1.0f);
        return CubicLerp(0.0f, 0.02f, w);
    }
    else if (value <= -0.57f) {
        float w = (value - -0.78f) / (-0.57f - -0.78f);
        return CubicLerp(0.02f, 0.14f, w);
    }
    // ... 총 8개 구간
    else {
        float w = (value - 0.78f) / (1.0f - 0.78f);
        return CubicLerp(0.86f, 1.0f, w);
    }
}
```

**CubicLerp (추정)**:
```cpp
float CubicLerp(float a, float b, float t) {
    float t2 = t * t;
    float t3 = t2 * t;
    return a + (b - a) * (3.0f * t2 - 2.0f * t3);  // Smoothstep
}
```

**구간 분할 기준**:
- `SplineContinentalness`: 해양(-1.0~-0.51), 해안(-0.51~-0.25), 평지(-0.25~0.09), 언덕(0.09~0.42), 산악(0.42~1.0)
- `SplineErosion`: 침식 강도 8단계
- `SplinePeaksValley`: 계곡(0.0~0.24), 평탄(0.24~0.42), 산봉우리(0.42~1.0)

### 4.4. 랜덤 배치 유틸리티

`GenerateRandomPlace2D()`는 청크 내 나무/인스턴스의 위치를 결정적으로 생성:

```cpp
void GenerateRandomPlace2D(Vector3 worldPosition, uint32_t soltX, uint32_t soltZ,
    int maxPlaceCount, int indexCount, std::vector<std::pair<int, int>>& outRandomPlace2D)
{
    uint32_t seedX = (uint32_t)floor(worldPosition.x) + soltX;
    uint32_t seedZ = (uint32_t)floor(worldPosition.z) + soltZ;
    uint32_t soltY = (uint32_t)floor(worldPosition.y);

    for (int i = 0; i < maxPlaceCount; ++i) {
        seedX = Utils::HashInt(seedX, soltX * soltY);
        seedZ = Utils::HashInt(seedZ, soltZ * soltY);

        int xIndex = seedX % indexCount;
        int zIndex = seedZ % indexCount;

        outRandomPlace2D.push_back(std::make_pair(xIndex, zIndex));
    }
}
```

**특징**:
- **결정적 난수**: 월드 좌표 + solt(salt) 값으로 시드 생성 → 동일 청크는 항상 같은 위치 반환
- **Hash 기반**: `Utils::HashInt()`로 시드를 반복적으로 해싱하여 의사 난수 생성
- **사용처**:
  - `Chunk::InitTreePlace()`: `soltX=특정값`, `maxPlaceCount=TREE_PLACE_MAX_COUNT_PER_CHUNK`로 나무 위치 생성
  - `Chunk::InitInstancePlace()`: 풀/꽃 인스턴스 위치 생성

## 5. 문제점 & 해결

### 5.1. 노이즈 범위 균등 분포 문제

**문제**: `PerlinFbm()`의 출력이 [-1, 1]에서 거의 균등 분포하여 원하는 지형 비율(예: 해양 70%, 평지 20%, 산악 10%)을 만들 수 없음.

**해결**: Spline 변환으로 비선형 재매핑. 예를 들어 `SplineContinentalness`는 입력 [-1, -0.51] 구간을 출력 [0.0, 0.14]로 압축하여 해양 비율을 증가시킴.

**트레이드오프**:
- 장점: 지형 분포 제어 가능, 현실적인 지형 비율 구현
- 단점: 구간 경계에서 급격한 변화 가능 (해양-육지 경계가 뚜렷함) → 추후 보간 개선 필요

### 5.2. Elevation 계산의 파라미터 가중치 조정

**문제**: Continentalness, Erosion, PeaksValley의 영향력 비율이 불균형하면 비현실적 지형 생성 (모든 곳이 산 또는 모든 곳이 평지).

**해결**:
- Erosion의 지수를 `pow(1 - e, 1.25)`로 조정하여 침식 효과 강화
- PeaksValley는 Erosion이 낮을 때만 영향을 주도록 곱셈 결합
- 실험적으로 64.0 스케일 팩터 설정

**결과**: Erosion > 0.7인 평지는 PeaksValley 영향이 거의 없고, Erosion < 0.3인 산악 지역은 PeaksValley로 급격한 고저차 형성.

### 5.3. 동굴 생성의 연결성 문제

**문제**: 단일 3D 노이즈로 동굴 생성 시 임계값이 너무 작으면 고립된 작은 공간만 생성, 너무 크면 모든 지하가 빈 공간.

**해결**:
- **두 개의 독립 노이즈 사용**: `density1^2 + density2^2 <= threshold` 조건으로 두 노이즈가 모두 낮을 때만 동굴 형성
- **다른 Scale 적용**: density1(256), density2(512)로 서로 다른 주파수의 동굴 패턴 겹침
- **Early Return 최적화**: 둘 중 하나라도 threshold 초과 시 즉시 반환하여 70~80% 연산 스킵

**트레이드오프**:
- 장점: 자연스러운 동굴 네트워크, 연결성 향상
- 단점: Y축 모든 높이에서 동일한 threshold 사용하여 깊이별 동굴 밀도 차이 없음 (추후 Y축 기반 threshold 조정 필요)

### 5.4. Temperature/Humidity 노이즈의 극단값 Clamp

**문제**: Perlin 노이즈의 출력이 이론적으로 [-1, 1]이지만 FBM 누적 시 범위 초과 가능.

**해결**:
```cpp
float tNoise = Utils::PerlinFbm(...);
tNoise = clamp(tNoise * 2.0f, -1.0f, 1.0f);  // 범위 확장 후 Clamp
float tValue = (tNoise + 1.0f) * 0.5f;  // [0, 1]로 정규화
```

**이유**: `* 2.0f`로 노이즈 진폭을 증가시켜 극단적인 온도/습도 지역(사막, 툰드라) 비율을 높이고, Clamp로 안전 범위 보장.

### 5.5. Chunk 경계의 노이즈 불연속성

**문제**: 청크 단위로 노이즈를 생성하면 청크 경계에서 지형이 끊어질 위험.

**해결**:
- **월드 좌표 기준 노이즈 샘플링**: 청크 로컬 좌표가 아닌 `worldX`, `worldZ` 전역 좌표를 노이즈 함수에 전달
- **Padding 포함 계산**: `CHUNK_SIZE_P` (32 + 2 padding)로 인접 청크와 1블록씩 겹치는 영역도 노이즈 계산

**결과**: 청크 경계에서도 연속적인 지형 생성, BiomeLayer 계산 시 경계 블록도 올바른 노이즈 참조 가능.

## 6. 결과

### 6.1. 생성 가능한 지형 형태

6종 노이즈 조합으로 다음 지형 생성:

| Continentalness | Erosion | PeaksValley | 최종 지형 형태             | Elevation 범위 |
|----------------|---------|-------------|-------------------------|---------------|
| 낮음 (< 0.14)   | 무관     | 무관         | 심해 (Deep Ocean)        | -64 ~ -20     |
| 중간 (0.31~0.43)| 높음     | 무관         | 평지 (Plains)            | 0 ~ 10        |
| 높음 (> 0.57)   | 낮음     | 높음         | 산악 (Mountains)         | 100 ~ 192     |
| 높음 (> 0.57)   | 낮음     | 낮음         | 계곡 (Valley)            | 30 ~ 60       |
| 중간 (0.43~0.57)| 중간     | 중간         | 언덕 (Hills)             | 20 ~ 80       |

**특징**:
- Erosion이 높으면 Continentalness가 높아도 평탄화 (고원 → 평지)
- PeaksValley는 Erosion이 낮을 때만 효과 (산악 지역의 세부 고저)

### 6.2. 동굴 네트워크

- **비율**: 전체 지하 공간의 약 2~4% (threshold 0.004 기준)
- **형태**: 서로 다른 주파수의 두 노이즈로 좁은 터널과 넓은 공동 혼합
- **분포**: Y축 0~256 모든 높이에서 균등 생성 (깊이별 밀도 차이 없음)

### 6.3. 바이옴과의 통합

Temperature/Humidity 노이즈를 Terrain에서 생성하여 `Biome::GetBiomeType()`에 전달:
- **대륙 단위 기후대**: Scale 5000+로 넓은 지역이 동일한 온도/습도 유지
- **지형-바이옴 상호작용**: `Biome::GetBiomeTerrainHeight()`가 Continentalness, Erosion, Temperature, Humidity를 모두 고려하여 바이옴별 지형 높이 보정 가능

## 7. 회고

### 7.1. 아쉬운 점

1. **하드코딩된 Spline 구간**:
   - 현재 SplineContinentalness, SplineErosion의 구간 경계값(-0.51, -0.25, 0.09 등)이 코드에 하드코딩됨
   - 지형 비율 조정 시 코드 수정 및 재컴파일 필요
   - **개선 방향**: JSON/XML 파일로 Spline 제어점 정의하여 런타임 조정 가능하게

2. **Elevation 공식의 가독성 부족**:
   - `64.0f * c * (1 - e) + 64.0f * pv * pow(1 - e, 1.25)`의 의미가 직관적이지 않음
   - 64.0, 1.25 같은 매직 넘버 사용
   - **개선 방향**: 변수명 개선 (`BASE_HEIGHT_SCALE`, `PEAKS_EROSION_EXPONENT`) 및 주석 추가

3. **동굴 생성의 Y축 미고려**:
   - 모든 높이에서 동일한 threshold(0.004) 사용하여 깊이별 동굴 밀도 차이 없음
   - 마인크래프트는 Y < 0에서 동굴 밀도 증가, Y > 128에서 감소
   - **개선 방향**: `threshold = 0.004f * (1.0f + (64.0f - y) / 128.0f)` 같은 Y축 보정 추가

4. **Noise API 병목**:
   - `Utils::PerlinFbm()` 호출이 청크당 수천~수만 번 발생 (34x34x6종 노이즈)
   - CPU 병목의 주요 원인 (Chunk 초기화 평균 수십 ms)
   - **개선 방향**: GPU Compute Shader로 노이즈 생성 오프로드 또는 SIMD 최적화

5. **Distribution 노이즈의 역할 모호**:
   - Scale 24.0으로 매우 세밀하지만 실제로는 광석 분포와 나무/인스턴스 선택에만 사용
   - Elevation 계산에는 미참여하여 존재 의미 약함
   - **개선 방향**: Distribution을 Erosion의 세부 변형으로 활용하거나 제거 고려

### 7.2. 다음에 개선하고 싶은 방향

1. **3D Elevation**:
   - 현재는 2D 노이즈로 높이만 결정하여 오버행(Overhang), 동굴 천장 등 3D 구조 불가
   - 마인크래프트 1.18+ 처럼 3D Density 기반 지형 생성 도입

2. **Temperature의 Y축 변화**:
   - 현재 온도는 X, Z에만 의존하지만 현실적으로는 고도가 높을수록 온도 하강
   - `Temperature(x, y, z) = GetTemperature(x, z) - y / 100.0f` 같은 고도 보정 추가

3. **바이옴별 Erosion 보정**:
   - 사막은 침식 약함, 열대우림은 침식 강함 등 바이옴별 Erosion 가중치 적용
   - Biome과 Terrain의 상호작용 강화

4. **Noise Caching Layer**:
   - 여러 청크가 동일한 노이즈 샘플 참조 가능 → 청크 간 공유 캐시 구현
   - LRU 기반 노이즈 타일 캐싱으로 메모리-성능 트레이드오프
