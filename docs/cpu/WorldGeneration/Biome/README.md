## 1. 개요

- **바이옴 시스템(Biome System)**은 월드의 생태학적 지역을 결정하는 시스템
- 기후 매개변수(continentalness, erosion, temperature, humidity)를 기반으로 13개의 바이옴 타입 중 하나를 선택
- 각 바이옴은 고유한 지형 높이, 나무, 인스턴스(풀, 꽃), 색상을 가짐
- 프로젝트 전체에서 **월드 생성(World Generation)의 핵심 단계**로, 지형의 다양성과 자연스러운 전환을 담당

## 2. 도입 동기

- **자연스러운 월드 다양성**: 단조로운 지형 대신 사막, 숲, 평원, 툰드라 등 다양한 생태계 표현
- **기후 기반 생태계**: Minecraft의 기후 시스템을 참고하여, 온도와 습도로 현실적인 바이옴 분포 생성
- **부드러운 바이옴 전환**: 바이옴 경계가 급격하게 변하지 않고 자연스럽게 블렌딩되도록 구현
- **바이옴별 콘텐츠 차별화**: 각 바이옴마다 고유한 나무, 식생, 색상을 부여하여 탐험의 재미 제공

## 3. 핵심 아이디어

### 3.1 4차원 기후 공간에서의 거리 기반 바이옴 선택

```
Weight = √(coEffi[0] * (c - c_biome)² + coEffi[1] * (e - e_biome)²
          + coEffi[2] * (t - t_biome)² + coEffi[3] * (h - h_biome)²)
```

- 각 위치(x, z)마다 4개의 기후 값(c, e, t, h) 계산
- 13개 바이옴 각각과의 **유클리드 거리(가중치 적용)**를 계산
- 가장 가까운 바이옴을 해당 위치의 바이옴으로 결정
- 계수(coEffi): `[10.0, 1.0, 3.0, 3.0]`로 continentalness가 가장 중요

### 3.2 바이옴 경계 블렌딩

```cpp
if (weightDiff < threshold) { // 두 바이옴의 거리 차이가 작으면
    uint32_t hash = HashInt(x) ^ HashInt(z);
    if (hash % 11 == 0) {
        return 두 번째로 가까운 바이옴; // 확률적 블렌딩
    }
}
```

- 1위와 2위 바이옴의 거리 차이가 `threshold(0.005)` 미만이면 블렌딩 영역
- 해시 기반 랜덤으로 약 9% 확률로 두 번째 바이옴 선택
- 자연스러운 전환 효과 생성

### 3.3 역거리 가중치 보간(Inverse Distance Weighting)으로 높이 계산

```
Height = BiomeBaseHeight + (ElevationScale × Elevation(c, e, pv))
```

- 단일 바이옴의 높이가 아닌, **모든 바이옴의 높이를 가중치로 블렌딩**
- 가중치 `w = 1 / (distance²)^5` - 거리가 가까울수록 영향력 급증
- 부드러운 높이 전환으로 바이옴 경계의 지형 단절 방지

## 4. 구현 내용

### 4.1 바이옴 타입 정의 (13개)

| 바이옴           | BaseColor   | 기후 파라미터 (c, e, t, h) | baseHeight | elevationScale | 나무          | 인스턴스         |
| ---------------- | ----------- | -------------------------- | ---------- | -------------- | ------------- | ---------------- |
| **Ocean**        | 파랑        | (0.0, 0.5, 0.5, 0.5)       | 32.0       | 0.025          | 없음          | 해초, 켈프       |
| **Tundra**       | 흰색        | (0.7, 0.5, 0.125, 0.2)     | 108.0      | 0.025          | Spruce        | 없음             |
| **Taiga**        | 어두운 녹색 | (0.7, 0.5, 0.34, 0.66)     | 96.0       | 0.25           | Spruce        | 풀, 고사리, 베리 |
| **Plains**       | 밝은 녹색   | (0.7, 0.5, 0.435, 0.16)    | 64.0       | 0.025          | Oak           | 풀, 데이지, 튤립 |
| **Swamp**        | 청록색      | (0.7, 0.5, 0.53, 0.88)     | 56.0       | 0.025          | Oak, Mangrove | 풀, 난초, 버섯   |
| **Forest**       | 짙은 녹색   | (0.7, 0.5, 0.53, 0.66)     | 64.0       | 1.5            | Oak, Birch    | 풀, 장미, 백합   |
| **Shrubland**    | 연두색      | (0.7, 0.5, 0.53, 0.44)     | 64.0       | 0.01           | Oak, Cherry   | 풀, 민들레, 튤립 |
| **Desert**       | 주황색      | (0.7, 0.5, 0.81, 0.125)    | 80.0       | 0.01           | Cactus        | 고목             |
| **Rainforest**   | 진한 녹색   | (0.7, 0.5, 0.84, 0.88)     | 64.0       | 1.5            | Oak, Jungle   | 풀, 고사리       |
| **Seasonforest** | 연한 녹색   | (0.7, 0.5, 0.84, 0.66)     | 64.0       | 1.5            | Oak, Birch    | 풀, 장미, 튤립   |
| **Savanna**      | 황토색      | (0.7, 0.5, 0.84, 0.44)     | 70.0       | 0.01           | Oak, Acacia   | 풀               |
| **Snowy Taiga**  | 연한 녹색   | (0.7, 0.5, 0.28, 0.66)     | 96.0       | 0.75           | Spruce        | 풀, 고사리, 베리 |

### 4.2 주요 함수

**`GetBiomeType(c, e, t, h, x, z)`**

```cpp
// 1. 13개 바이옴 각각과의 가중치 거리 계산
for (각 바이옴) {
    weight = √(10×(c-c_biome)² + 1×(e-e_biome)² + 3×(t-t_biome)² + 3×(h-h_biome)²)
    weights.push_back({weight, biome_type})
}

// 2. 거리순 정렬
sort(weights)

// 3. 블렌딩 처리
if (weights[0]과 weights[1]의 차이 < 0.005) {
    hash = HashInt(x) ^ HashInt(z)
    if (hash % 11 == 0) return weights[1].second
}

return weights[0].second
```

**`GetBiomeTerrainHeight(c, e, pv, t, h)`**

```cpp
// 1. 모든 바이옴에 대해 가중치 계산
for (각 바이옴) {
    diff = √(10×(c-c_biome)² + 1×(e-e_biome)² + 5×(t-t_biome)² + 5×(h-h_biome)²)
    weight = (1 / diff²)^5  // 역거리 가중치의 5제곱

    sumBiomeBaseHeight += weight × baseHeight
    sumElevationScale += weight × elevationScale
    sumWeight += weight
}

// 2. 가중 평균으로 최종 높이 계산
biomeBaseHeight = sumBiomeBaseHeight / sumWeight
elevationScale = sumElevationScale / sumWeight
elevation = Terrain::GetElevation(c, e, pv)

height = biomeBaseHeight + (elevationScale × elevation)
return clamp(height, 1.0, 255.0)
```

### 4.3 자료구조

**BiomeWeightParams**

```cpp
struct BiomeWeightParams {
    float continentalness;   // 대륙성 (0.0=해양, 0.7=대륙)
    float erosion;           // 침식도 (0.5 고정)
    float temperature;       // 온도 (0.125=한랭, 0.84=열대)
    float humidity;          // 습도 (0.125=건조, 0.88=습함)
    float elevationScale;    // 고도 변화 스케일 (0.01~1.5)
    float baseHeight;        // 기본 높이 (32~108)
};
```

**BiomeTypeInfo**

- `m_baseColor`: 바이옴 맵에서 색상 표시용
- `m_maxTreeCountPerChunk`: 청크당 최대 나무 생성 개수
- `m_maxInstanceCountPerChunk`: 청크당 최대 인스턴스 생성 개수
- `m_instances`: 이 바이옴에서 생성 가능한 인스턴스 타입 목록
- `m_trees`: 이 바이옴에서 생성 가능한 나무 타입 목록
- `m_weightParams`: 기후 및 높이 파라미터

## 5. 문제점 & 해결

### 5.1 바이옴 경계의 급격한 전환

**문제**: 단순 최근접 바이옴 선택 시 경계가 너무 뚜렷하게 보임

**해결**:

- 두 바이옴의 거리 차이가 작을 때(`< 0.005`) 해시 기반 랜덤 블렌딩
- 약 9% 확률로 두 번째 바이옴 선택
- 픽셀 단위로 섞여 육안으로는 자연스러운 전환 효과

### 5.2 바이옴 경계에서 지형 높이 단절

**문제**: 바이옴마다 baseHeight가 다르면 경계에서 절벽 발생

**해결**:

- `GetBiomeTerrainHeight()`에서 **역거리 가중치 보간(IDW)** 사용
- 모든 바이옴의 높이를 가중 평균 → 경계 근처에서 부드러운 높이 변화
- 가중치 `(1/distance²)^5`로 거리가 가까운 바이옴의 영향력 극대화

### 5.3 continentalness의 과도한 영향력

**문제**: 초기 계수 동일 시 온도/습도보다 대륙성이 지나치게 우세

**해결**:

- `coEffi = [10.0, 1.0, 3.0, 3.0]`으로 조정
- continentalness는 10배 가중치 → Ocean과 육지 바이옴의 명확한 구분
- temperature와 humidity는 3배 가중치 → 육지 내 세부 바이옴 구분

## 6. 결과

### 6.1 바이옴 분포

- **해양-육지 경계**: continentalness 가중치 10배로 명확한 해안선 형성
- **기후대 구분**:
  - 한랭 지역: Tundra, Snowy Taiga (temperature ≤ 0.34)
  - 온대 지역: Plains, Forest, Shrubland (0.435 ≤ temperature ≤ 0.53)
  - 열대 지역: Desert, Savanna, Rainforest (temperature ≥ 0.81)
- **습도 구분**:
  - 건조: Desert (humidity = 0.125)
  - 보통: Plains, Savanna (humidity ≤ 0.44)
  - 습함: Swamp, Rainforest (humidity ≥ 0.88)

### 6.2 자연스러운 전환

- 블렌딩 threshold (0.005)로 바이옴 경계가 1~2 블록 폭으로 섞임
- IDW로 높이 전환이 부드러워 절벽/계단 현상 없음
- 육안으로는 자연스러운 그라데이션 효과

### 6.3 성능

- `GetBiomeType()`: 13개 바이옴 × 4차원 거리 계산 + 정렬 → 청크 생성 시 1회
- `GetBiomeTerrainHeight()`: 13개 바이옴 × IDW 계산 → 블록마다 1회
- 월드 생성 단계에서만 실행되므로 런타임 성능 영향 없음

## 7. 회고

### 7.1 아쉬운 점

- **바이옴 개수 고정**: 13개 바이옴이 하드코딩되어 확장성이 낮음
  - 새 바이옴 추가 시 `BiomeTypeInfoSet` 생성자를 직접 수정해야 함
- **계수 튜닝의 어려움**: `coEffi` 값이 하드코딩되어 있어 실험적 조정 필요
  - 파라미터 파일로 외부화하면 더 유연한 조정 가능
- **블렌딩의 단순성**: 해시 기반 랜덤 블렌딩은 간단하지만, Voronoi 기반 블렌딩이나 Perlin Worms 같은 고급 기법 부재
- **3D 바이옴**: 현재는 x-z 평면에서만 바이옴 결정
  - 동굴/지하 바이옴을 위해서는 y축 고려 필요

### 7.2 다음에 개선하고 싶은 방향

- **설정 파일 기반 바이옴 정의**: JSON/TOML로 바이옴 파라미터 외부화
- **동적 바이옴 개수**: 런타임에 바이옴 추가/제거 가능하도록 리팩토링
- **고급 블렌딩**: Voronoi Diagram 기반 자연스러운 경계 생성
- **3D 바이옴**: 고도별 다른 바이옴 (예: 고산 바이옴, 지하 바이옴)
- **바이옴 변이(Variant)**: 같은 기본 바이옴에서 파생된 변종 (예: Plains → Sunflower Plains)
