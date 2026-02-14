# WorldMap

## 1. 개요

WorldMap 시스템은 Voxen 엔진에서 실시간으로 생성되는 2D 바이옴 맵과 기후 맵을 관리하는 모듈입니다. 카메라 위치를 중심으로 512x512 픽셀(바이옴 맵) 또는 전체 렌더 거리(기후 맵)의 월드 영역을 2D 텍스처로 변환하여 미니맵 UI로 표시합니다.

엔진 전체에서 다음 역할을 수행합니다:
- **실시간 바이옴 가시화**: `GetBiomeMapColor()`로 각 픽셀의 바이옴 타입과 지형 높이를 색상으로 변환
- **Scrolling 최적화**: 카메라 이동 시 전체 재계산 대신 `ShiftMapData()` + `UpdateMapData()`로 부분 갱신
- **Multi-resolution 지원**: 바이옴 맵(저해상도, 픽셀당 ~16블록)과 기후 맵(고해상도, 픽셀당 1블록) 동시 관리
- **GPU 텍스처 연동**: 계산된 맵 데이터를 DirectX 11 텍스처로 업데이트하여 셰이더에서 렌더링

## 2. 도입 동기

마인크래프트의 미니맵 기능처럼 플레이어가 주변 지형과 바이옴을 한눈에 파악할 수 있는 UI를 목표로 했습니다.

### 기존 방식의 한계
1. **정적 텍스처**: 미리 생성된 월드 맵 텍스처는 절차적 생성 엔진에서 불가능 (무한 월드)
2. **전체 재계산**: 카메라 이동마다 512x512 픽셀 전체를 노이즈 함수로 재계산하면 병목 (매 프레임 수십 ms)
3. **해상도 트레이드오프**: 높은 해상도(블록 단위)는 넓은 범위 표시 불가, 낮은 해상도는 디테일 부족

### 목표
- **실시간 생성**: 카메라 위치 기반으로 항상 최신 바이옴 맵 표시
- **Scrolling 최적화**: 카메라 이동 시 새로운 영역만 계산하여 성능 확보
- **Multi-resolution**: 바이옴 맵(넓은 범위)과 기후 맵(세밀한 디테일) 두 가지 해상도 제공
- **색상 인코딩**: 바이옴 타입과 지형 높이를 RGB 색상으로 직관적 표현

## 3. 핵심 아이디어

### 3.1. 두 가지 해상도의 맵

WorldMap은 용도별로 두 가지 맵을 관리:

| 맵 종류      | 버퍼 크기 | 픽셀당 월드 크기 | 총 커버 범위                  | 용도                     |
|-------------|----------|-----------------|------------------------------|-------------------------|
| Biome Map   | 512x512  | ~16 블록         | ~8192 x 8192 블록             | 넓은 범위 바이옴 표시     |
| Climate Map | 전체*    | 1 블록           | CHUNK_COUNT × CHUNK_SIZE 블록 | 세밀한 온도/습도 맵       |

*Climate Map 버퍼 크기 = `ChunkManager::CHUNK_COUNT * Chunk::CHUNK_SIZE`

**Biome Map 계산**:
```cpp
static const UINT BIOME_MAP_BUFFER_SIZE = 512;
static const UINT BIOME_MAP_WORLD_SIZE_PER_PIXEL =
    (ChunkManager::CHUNK_COUNT * Chunk::CHUNK_SIZE * 2) / BIOME_MAP_BUFFER_SIZE;
static const UINT BIOME_MAP_WORLD_SIZE =
    BIOME_MAP_BUFFER_SIZE * BIOME_MAP_WORLD_SIZE_PER_PIXEL;
```

**예시** (CHUNK_COUNT=32, CHUNK_SIZE=32인 경우):
- `CHUNK_COUNT * CHUNK_SIZE * 2 = 32 * 32 * 2 = 2048` 블록
- `WORLD_SIZE_PER_PIXEL = 2048 / 512 = 4` 블록/픽셀
- `BIOME_MAP_WORLD_SIZE = 512 * 4 = 2048` 블록 (총 커버 범위)

**핵심**:
- Biome Map: 낮은 해상도로 넓은 범위 커버 → 전략적 지형 파악
- Climate Map: 블록 단위 해상도 → 정밀한 온도/습도 디버깅

### 3.2. Scrolling 최적화 (Shift + Update 패턴)

카메라 이동 시 전체 재계산 대신 **기존 데이터 이동 + 새 영역만 계산** 방식 사용:

**알고리즘**:
```cpp
void Update(Vector3 cameraPosition) {
    // 1. 새로운 offset position 계산
    Vector3 newOffsetPosition = CalcOffsetPos(cameraPosition, WORLD_SIZE_PER_PIXEL);
    Vector3 offsetDiff = m_offsetPosition - newOffsetPosition;

    if (offsetDiff.Length() > 0) {
        m_offsetPosition = newOffsetPosition;

        // 2. 이동 방향 결정
        int dx = Signf(offsetDiff.x);  // -1, 0, +1
        int dz = -Signf(offsetDiff.z);

        // 3. 기존 데이터 이동 (shift)
        ShiftBiomeMapData(dx, dz);

        // 4. 새 영역만 계산 (update)
        UpdateBiomeMapData(dx, dz);

        // 5. GPU 텍스처 업데이트
        UpdateTexture2DBuffer(Graphics::biomeMapBuffer, m_biomeMapData, 512, 512);
    }
}
```

**ShiftBiomeMapData() - 데이터 시프트**:
```cpp
void ShiftBiomeMapData(int dx, int dz) {
    // dx > 0: 카메라가 +X 방향 이동 → 맵 데이터는 -X 방향 시프트 (오른쪽에서 왼쪽으로)
    int startX = (dx <= 0) ? 0 : BUFFER_SIZE - 1;
    int endX = (dx <= 0) ? BUFFER_SIZE : -1;
    int stepX = (dx <= 0) ? 1 : -1;

    // 동일하게 Z축 처리
    int startZ = (dz <= 0) ? 0 : BUFFER_SIZE - 1;
    int endZ = (dz <= 0) ? BUFFER_SIZE : -1;
    int stepZ = (dz <= 0) ? 1 : -1;

    // 역방향 루프로 데이터 덮어쓰기 방지
    for (int z = startZ; z != endZ; z += stepZ) {
        for (int x = startX; x != endX; x += stepX) {
            int nx = x + dx;
            int nz = z + dz;

            if (nx < 0 || nx >= BUFFER_SIZE || nz < 0 || nz >= BUFFER_SIZE)
                continue;

            m_biomeMapData[nx + nz * BUFFER_SIZE] = m_biomeMapData[x + z * BUFFER_SIZE];
        }
    }
}
```

**핵심**:
- **역방향 루프**: dx > 0이면 오른쪽에서 왼쪽으로 복사하여 덮어쓰기 방지
- **메모리 재사용**: 512x512 중 511x512 또는 512x511 픽셀은 그대로 재사용
- **새 영역만 비워짐**: 시프트 후 한 열 또는 한 행이 비워져 UpdateBiomeMapData()에서 채워짐

**UpdateBiomeMapData() - 새 영역 계산**:
```cpp
void UpdateBiomeMapData(int dx, int dz) {
    // X축 이동 시: 한 열(column) 갱신
    if (dx != 0) {
        int x = (dx < 0) ? BUFFER_SIZE - 1 : 0;  // 왼쪽 or 오른쪽 끝
        for (int z = 0; z < BUFFER_SIZE; ++z) {
            m_biomeMapData[x + z * BUFFER_SIZE] = GetBiomeMapColor(x, z);
        }
    }

    // Z축 이동 시: 한 행(row) 갱신
    if (dz != 0) {
        int z = (dz < 0) ? BUFFER_SIZE - 1 : 0;  // 위 or 아래 끝
        for (int x = 0; x < BUFFER_SIZE; ++x) {
            m_biomeMapData[x + z * BUFFER_SIZE] = GetBiomeMapColor(x, z);
        }
    }
}
```

**성능 비교**:
- **전체 재계산**: 512 × 512 = 262,144 픽셀 계산
- **Scrolling**: 512 (한 열) 또는 1024 (대각선: 한 열 + 한 행) 픽셀만 계산
- **속도 향상**: 약 **256배** ~ **512배** (대부분의 경우)

### 3.3. 픽셀 좌표 → 월드 좌표 변환

맵의 중심은 항상 카메라 위치이며, 각 픽셀은 월드 좌표로 변환되어 노이즈 샘플링:

**좌표 변환 공식**:
```cpp
RGBA_UINT GetBiomeMapColor(int x, int z) {
    // 맵 버퍼의 (x, z) 픽셀 → 월드 좌표 계산
    int worldX = (int)m_biomeMapOffsetPosition.x - (BIOME_MAP_WORLD_SIZE / 2)
                 + BIOME_MAP_WORLD_SIZE_PER_PIXEL * x;
    int worldZ = (int)m_biomeMapOffsetPosition.z + (BIOME_MAP_WORLD_SIZE / 2)
                 - BIOME_MAP_WORLD_SIZE_PER_PIXEL * z;

    // 월드 좌표에서 노이즈 샘플링
    float continentalness = Terrain::GetContinentalness(worldX, worldZ);
    float erosion = Terrain::GetErosion(worldX, worldZ);
    float temperature = Terrain::GetTemperature(worldX, worldZ);
    float humidity = Terrain::GetHumidity(worldX, worldZ);
    float elevation = Biome::GetBiomeTerrainHeight(...);

    // 바이옴 타입 결정
    BIOME_TYPE biomeType = Biome::GetBiomeType(continentalness, erosion, temperature, humidity, worldX, worldZ);

    // 색상 변환 (아래 섹션 참고)
    return ColorEncoding(biomeType, elevation);
}
```

**좌표 구조**:
```
카메라 위치 (offsetPosition)
         ↓
  ┌─────────────────┐
  │                 │ ← worldX = offsetX - (WORLD_SIZE / 2) + WORLD_SIZE_PER_PIXEL * x
  │    [x, z]       │
  │    픽셀         │
  │                 │
  └─────────────────┘
  ← WORLD_SIZE →
```

**예시**:
- offsetPosition = (1000, 0, 1000)
- WORLD_SIZE = 2048, WORLD_SIZE_PER_PIXEL = 4
- 픽셀 (0, 0): worldX = 1000 - 1024 + 0 = -24, worldZ = 1000 + 1024 - 0 = 2024
- 픽셀 (256, 256): worldX = 1000 - 1024 + 1024 = 1000, worldZ = 1000 + 1024 - 1024 = 1000 (중앙)

### 3.4. 바이옴 → 색상 인코딩

각 픽셀의 색상은 **바이옴 기본 색상 + 지형 높이 보정**으로 계산:

**알고리즘**:
```cpp
RGBA_UINT GetBiomeMapColor(int x, int z) {
    // 1. 바이옴 결정
    BIOME_TYPE biomeType = Biome::GetBiomeType(...);
    RGBA_UINT biomeBaseColor = Biome::GetBaseColor(biomeType);

    // 2. 수중 지형: 파란색 혼합
    if (elevation < 64.0f) {  // WATER_HEIGHT_LEVEL
        biomeBaseColor.r = (uint8_t)(biomeBaseColor.r * 0.8 + 6 * 0.2);
        biomeBaseColor.g = (uint8_t)(biomeBaseColor.g * 0.8 + 8 * 0.2);
        biomeBaseColor.b = (uint8_t)(biomeBaseColor.b * 0.8 + 255 * 0.2);
    }

    // 3. 고도 기반 명도 조정
    float brightnessFactor = min(1.5f, (elevation / 63.0f));
    biomeBaseColor.r = (uint8_t)clamp((int)(biomeBaseColor.r * brightnessFactor), 0, 255);
    biomeBaseColor.g = (uint8_t)clamp((int)(biomeBaseColor.g * brightnessFactor), 0, 255);
    biomeBaseColor.b = (uint8_t)clamp((int)(biomeBaseColor.b * brightnessFactor), 0, 255);

    return biomeBaseColor;
}
```

**색상 효과**:
- **바이옴 기본 색상**: `Biome::GetBaseColor(biomeType)`에서 가져옴 (예: Plains=녹색, Desert=노란색)
- **수중 지형 (elevation < 64)**:
  - R: 원래의 80% + 파란색(6)의 20%
  - G: 원래의 80% + 파란색(8)의 20%
  - B: 원래의 80% + 파란색(255)의 20%
  - 결과: 모든 바이옴이 파란색으로 변하여 해양/호수 표시
- **고도 보정**:
  - `brightnessFactor = min(1.5, elevation / 63)`
  - elevation=0: 0배 (검은색)
  - elevation=63: 1배 (원래 색상)
  - elevation=94: 1.5배 (밝은 색상)
  - 결과: 높은 산은 밝게, 낮은 평지는 어둡게 → 지형 고도 시각화

**예시**:
- **Plains (elevation=70)**: 기본 녹색(0, 255, 0) × 1.11 = (0, 255, 0) (약간 밝음)
- **Ocean (elevation=50)**: 기본 파란색(0, 0, 255) + 수중 효과 → (1, 2, 255) × 0.79 = (0, 1, 201) (어두운 파란색)
- **Mountain (elevation=150)**: 기본 회색(128, 128, 128) × 1.5 = (192, 192, 192) (밝은 회색)

## 4. 구현 내용

### 4.1. 초기화 (Initialize)

**알고리즘**:
```cpp
bool WorldMap::Initialize(Vector3 cameraPosition) {
    // === Biome Map 초기화 ===
    {
        // 1. 버퍼 할당
        m_biomeMapData.resize(BIOME_MAP_BUFFER_SIZE * BIOME_MAP_BUFFER_SIZE);

        // 2. Offset position 계산 (카메라 중심)
        m_biomeMapOffsetPosition = Utils::CalcOffsetPos(cameraPosition, BIOME_MAP_WORLD_SIZE_PER_PIXEL);

        // 3. 전체 픽셀 계산
        for (int z = 0; z < BIOME_MAP_BUFFER_SIZE; ++z) {
            for (int x = 0; x < BIOME_MAP_BUFFER_SIZE; ++x) {
                m_biomeMapData[x + z * BIOME_MAP_BUFFER_SIZE] = GetBiomeMapColor(x, z);
            }
        }

        // 4. GPU 텍스처 업데이트
        if (!DXUtils::UpdateTexture2DBuffer(Graphics::biomeMapBuffer, m_biomeMapData,
                BIOME_MAP_BUFFER_SIZE, BIOME_MAP_BUFFER_SIZE)) {
            return false;
        }
    }

    // === Climate Map 초기화 (동일한 구조) ===
    {
        m_climateMapData.resize(CLIMATE_MAP_BUFFER_SIZE * CLIMATE_MAP_BUFFER_SIZE);
        m_climateMapOffsetPosition = Utils::CalcOffsetPos(cameraPosition, CLIMATE_MAP_WORLD_SIZE_PER_PIXEL);

        for (int z = 0; z < CLIMATE_MAP_BUFFER_SIZE; ++z) {
            for (int x = 0; x < CLIMATE_MAP_BUFFER_SIZE; ++x) {
                m_climateMapData[x + z * CLIMATE_MAP_BUFFER_SIZE] = GetClimateNoise(x, z);
            }
        }

        if (!DXUtils::UpdateTexture2DBuffer(Graphics::climateMapBuffer, m_climateMapData,
                CLIMATE_MAP_BUFFER_SIZE, CLIMATE_MAP_BUFFER_SIZE)) {
            return false;
        }
    }
    return true;
}
```

**CalcOffsetPos() - offset position 정렬**:
```cpp
// 추정 구현
Vector3 Utils::CalcOffsetPos(Vector3 position, int gridSize) {
    int x = (int)(floor(position.x / gridSize)) * gridSize;
    int z = (int)(floor(position.z / gridSize)) * gridSize;
    return Vector3((float)x, 0.0f, (float)z);
}
```

**목적**: 카메라 위치를 `WORLD_SIZE_PER_PIXEL` 단위로 정렬하여 픽셀 경계와 일치시킴

**결과**:
- Biome Map: 512x512 픽셀, 각 픽셀은 ~16블록 영역의 바이옴 색상
- Climate Map: 전체 렌더 거리 픽셀, 각 픽셀은 1블록의 온도/습도

### 4.2. 업데이트 (Update) - Scrolling 로직

**전체 흐름**:
```cpp
void WorldMap::Update(Vector3 cameraPosition) {
    // 1. 새로운 offset position 계산
    Vector3 newOffsetPosition = Utils::CalcOffsetPos(cameraPosition, BIOME_MAP_WORLD_SIZE_PER_PIXEL);

    // 2. 이동 거리 확인
    Vector3 offsetDiff = m_biomeMapOffsetPosition - newOffsetPosition;

    if (offsetDiff.Length() > 0) {  // 이동 발생
        m_biomeMapOffsetPosition = newOffsetPosition;

        // 3. 이동 방향 결정
        int dx = Utils::Signf(offsetDiff.x);  // -1, 0, +1
        int dz = -Utils::Signf(offsetDiff.z);  // Z축 반전 (화면 좌표계)

        // 4. 데이터 시프트
        ShiftBiomeMapData(dx, dz);

        // 5. 새 영역 계산
        UpdateBiomeMapData(dx, dz);

        // 6. GPU 업데이트
        DXUtils::UpdateTexture2DBuffer(Graphics::biomeMapBuffer, m_biomeMapData,
            BIOME_MAP_BUFFER_SIZE, BIOME_MAP_BUFFER_SIZE);
    }
}
```

**Signf() - 부호 함수**:
```cpp
// 추정 구현
int Utils::Signf(float value) {
    if (value > 0.0f) return 1;
    if (value < 0.0f) return -1;
    return 0;
}
```

**이동 케이스**:
| offsetDiff        | dx  | dz  | 의미                         | 갱신 영역      |
|-------------------|-----|-----|------------------------------|---------------|
| (+, 0)            | +1  | 0   | 카메라 +X 이동 → 맵 왼쪽 갱신  | 왼쪽 열       |
| (-, 0)            | -1  | 0   | 카메라 -X 이동 → 맵 오른쪽 갱신| 오른쪽 열     |
| (0, +)            | 0   | -1  | 카메라 +Z 이동 → 맵 위쪽 갱신  | 위쪽 행       |
| (0, -)            | 0   | +1  | 카메라 -Z 이동 → 맵 아래쪽 갱신| 아래쪽 행     |
| (+, +)            | +1  | -1  | 대각선 이동                   | 왼쪽 열 + 위쪽 행 |

### 4.3. ShiftBiomeMapData() 상세

**dx > 0 케이스 (카메라 +X 이동, 맵은 왼쪽으로 시프트)**:
```cpp
void ShiftBiomeMapData(int dx, int dz) {
    // dx > 0: startX = 511, endX = -1, stepX = -1
    // 오른쪽에서 왼쪽으로 복사 (역방향)
    int startX = (dx <= 0) ? 0 : BUFFER_SIZE - 1;  // 511
    int endX = (dx <= 0) ? BUFFER_SIZE : -1;       // -1
    int stepX = (dx <= 0) ? 1 : -1;                // -1

    // ... Z축 동일

    for (int z = startZ; z != endZ; z += stepZ) {
        for (int x = startX; x != endX; x += stepX) {  // x: 511 → 0
            int nx = x + dx;  // nx: 512 (범위 밖) → 1
            int nz = z + dz;

            if (nx < 0 || nx >= BUFFER_SIZE || nz < 0 || nz >= BUFFER_SIZE)
                continue;  // nx=512는 스킵

            // data[1] = data[0], data[2] = data[1], ..., data[511] = data[510]
            m_biomeMapData[nx + nz * BUFFER_SIZE] = m_biomeMapData[x + z * BUFFER_SIZE];
        }
    }
}
```

**시프트 효과**:
```
이전:
[0][1][2]...[510][511]

dx=+1 시프트 후:
[?][0][1]...[509][510]
 ^
 새 데이터 필요 (UpdateBiomeMapData에서 계산)
```

**역방향 루프 이유**:
정방향 루프 (x: 0 → 511) 사용 시:
```
data[1] = data[0]  // data[0] → data[1] 복사
data[2] = data[1]  // 하지만 data[1]은 이미 data[0]으로 덮어써짐!
→ 모든 데이터가 data[0]으로 오염
```

역방향 루프 (x: 511 → 0) 사용 시:
```
data[512] = data[511]  // (범위 밖, 스킵)
data[511] = data[510]  // data[510] → data[511]
data[510] = data[509]  // data[509] → data[510]
→ 덮어쓰기 전에 원본 데이터 읽기 완료
```

### 4.4. GetBiomeMapColor() 상세

**전체 알고리즘**:
```cpp
RGBA_UINT WorldMap::GetBiomeMapColor(int x, int z) {
    // 1. 픽셀 좌표 → 월드 좌표 변환
    int worldX = (int)m_biomeMapOffsetPosition.x - (BIOME_MAP_WORLD_SIZE / 2)
                 + BIOME_MAP_WORLD_SIZE_PER_PIXEL * x;
    int worldZ = (int)m_biomeMapOffsetPosition.z + (BIOME_MAP_WORLD_SIZE / 2)
                 - BIOME_MAP_WORLD_SIZE_PER_PIXEL * z;

    // 2. 노이즈 샘플링
    float continentalness = Terrain::GetContinentalness(worldX, worldZ);
    float erosion = Terrain::GetErosion(worldX, worldZ);
    float peaksValley = Terrain::GetPeaksValley(worldX, worldZ);
    float temperature = Terrain::GetTemperature(worldX, worldZ);
    float humidity = Terrain::GetHumidity(worldX, worldZ);

    // 3. 지형 높이 계산
    float elevation = Biome::GetBiomeTerrainHeight(continentalness, erosion, peaksValley, temperature, humidity);

    // 4. 바이옴 타입 결정
    BIOME_TYPE biomeType = Biome::GetBiomeType(continentalness, erosion, temperature, humidity, worldX, worldZ);

    // 5. 기본 색상 가져오기
    RGBA_UINT biomeBaseColor = Biome::GetBaseColor(biomeType);

    // 6. 수중 지형 보정
    if (elevation < 64.0f) {
        biomeBaseColor.r = (uint8_t)(biomeBaseColor.r * 0.8 + 6 * 0.2);
        biomeBaseColor.g = (uint8_t)(biomeBaseColor.g * 0.8 + 8 * 0.2);
        biomeBaseColor.b = (uint8_t)(biomeBaseColor.b * 0.8 + 255 * 0.2);
    }

    // 7. 고도 기반 명도 조정
    float brightnessFactor = min(1.5f, (elevation / 63.0f));
    biomeBaseColor.r = (uint8_t)clamp((int)(biomeBaseColor.r * brightnessFactor), 0, 255);
    biomeBaseColor.g = (uint8_t)clamp((int)(biomeBaseColor.g * brightnessFactor), 0, 255);
    biomeBaseColor.b = (uint8_t)clamp((int)(biomeBaseColor.b * brightnessFactor), 0, 255);

    return biomeBaseColor;
}
```

**노이즈 샘플링 비용**:
- `GetContinentalness()`: Perlin FBM 6 octaves
- `GetErosion()`: Perlin FBM 6 octaves
- `GetPeaksValley()`: Perlin FBM 6 octaves
- `GetTemperature()`: Perlin FBM 5 octaves
- `GetHumidity()`: Perlin FBM 5 octaves
- 총 28 octaves → 픽셀당 ~28번의 Perlin Noise 계산

**최적화**: Scrolling 덕분에 초기화 시 512x512 = 262,144 픽셀 계산 후, 이동 시 512~1024 픽셀만 추가 계산

### 4.5. Climate Map 구현

Climate Map은 Biome Map과 거의 동일한 구조이지만 데이터 타입과 해상도가 다름:

**GetClimateNoise()**:
```cpp
CLIMATE WorldMap::GetClimateNoise(int x, int z) {
    // 1. 픽셀 → 월드 좌표 (블록 단위)
    int worldX = (int)m_climateMapOffsetPosition.x - (CLIMATE_MAP_WORLD_SIZE / 2)
                 + CLIMATE_MAP_WORLD_SIZE_PER_PIXEL * x;
    int worldZ = (int)m_climateMapOffsetPosition.z + (CLIMATE_MAP_WORLD_SIZE / 2)
                 - CLIMATE_MAP_WORLD_SIZE_PER_PIXEL * z;

    // 2. 온도/습도 노이즈만 샘플링
    float temperature = Terrain::GetTemperature(worldX, worldZ);
    float humidity = Terrain::GetHumidity(worldX, worldZ);

    // 3. CLIMATE 구조체 반환
    return CLIMATE(temperature, humidity);
}
```

**CLIMATE 구조체 (추정)**:
```cpp
struct CLIMATE {
    float temperature;  // 0.0 ~ 1.0
    float humidity;     // 0.0 ~ 1.0

    CLIMATE(float t, float h) : temperature(t), humidity(h) {}
};
```

**용도**:
- Biome Map: RGB 색상 맵 → UI 표시
- Climate Map: 온도/습도 데이터 맵 → 셰이더에서 디버깅 시각화 또는 분석 용도

### 4.6. 렌더링 (RenderBiomeMap)

**알고리즘**:
```cpp
void WorldMap::RenderBiomeMap() {
    // 1. WorldMap 전용 viewport 설정
    Graphics::context->RSSetViewports(1, &Graphics::worldMapViewport);

    // 2. Back buffer에 렌더링
    Graphics::context->OMSetRenderTargets(1, Graphics::backBufferRTV.GetAddressOf(), nullptr);

    // 3. 셰이더 리소스 바인딩
    std::vector<ID3D11ShaderResourceView*> ppSRVs;
    ppSRVs.push_back(Graphics::biomeMapSRV.Get());     // t0: 바이옴 맵 텍스처
    ppSRVs.push_back(Graphics::worldPointSRV.Get());   // t1: 월드 포인트 텍스처 (카메라 위치?)
    Graphics::context->PSSetShaderResources(0, (UINT)ppSRVs.size(), ppSRVs.data());

    // 4. Pipeline State 설정 및 렌더링
    Graphics::SetPipelineStates(Graphics::biomeMapPSO);
    SimpleQuadRenderer::GetInstance()->Render();  // 전체 화면 쿼드

    // 5. 기본 viewport 복원
    Graphics::context->RSSetViewports(1, &Graphics::basicViewport);
}
```

**worldMapViewport (추정)**:
```cpp
// BIOME_MAP_UI_SIZE = 720
D3D11_VIEWPORT worldMapViewport = {
    0.0f, 0.0f,                  // TopLeftX, TopLeftY (화면 왼쪽 상단)
    720.0f, 720.0f,              // Width, Height
    0.0f, 1.0f                   // MinDepth, MaxDepth
};
```

**Pixel Shader (추정)**:
```hlsl
Texture2D biomeMapTexture : register(t0);
Texture2D worldPointTexture : register(t1);
SamplerState samplerState : register(s0);

float4 main(float2 uv : TEXCOORD) : SV_TARGET {
    // 바이옴 맵 샘플링
    float4 biomeColor = biomeMapTexture.Sample(samplerState, uv);

    // worldPoint로 카메라 위치 오버레이 (추정)
    float3 worldPoint = worldPointTexture.Sample(samplerState, uv).rgb;
    if (length(worldPoint) > 0) {
        biomeColor.rgb = lerp(biomeColor.rgb, float3(1, 0, 0), 0.5);  // 빨간 점
    }

    return biomeColor;
}
```

**결과**:
- 화면 좌측 상단 720x720 영역에 바이옴 맵 표시
- 카메라 위치는 빨간 점으로 표시 (worldPointSRV)

## 5. 문제점 & 해결

### 5.1. 픽셀당 노이즈 계산 비용

**문제**: `GetBiomeMapColor()`는 픽셀당 5개 노이즈 함수(총 28 octaves)를 호출하여 매우 느림. 초기화 시 512x512 = 262,144 픽셀 계산은 수백 ms 소요.

**해결**: Scrolling 최적화
- 초기화 후에는 카메라 이동 시 512~1024 픽셀만 계산
- 대부분의 프레임에서 계산 비용 0 (offsetDiff.Length() == 0)
- 이동 시에도 1~2ms 이내 처리

**트레이드오프**:
- 장점: 실시간 성능 확보 (60 FPS 유지)
- 단점: 초기 로딩 시간 증가 (수백 ms)

### 5.2. Shift 시 데이터 덮어쓰기 문제

**문제**: 정방향 루프로 데이터 시프트 시 원본 데이터가 덮어써져 모든 픽셀이 첫 픽셀 값으로 오염.

**해결**: 역방향 루프
```cpp
// dx > 0이면: x: 511 → 0 (역방향)
int startX = (dx <= 0) ? 0 : BUFFER_SIZE - 1;
int stepX = (dx <= 0) ? 1 : -1;
for (int x = startX; x != endX; x += stepX) { ... }
```

**결과**: 덮어쓰기 전에 원본 데이터 읽기 완료하여 정상 시프트

**트레이드오프**:
- 장점: 정확한 데이터 시프트
- 단점: 루프 방향 결정 로직 복잡성 증가

### 5.3. 수중/지상 색상 구분 모호함

**문제**: elevation < 64 (물 높이) 조건만으로는 깊은 바다와 얕은 해안의 색상 차이가 작음.

**해결**: 두 가지 보정 적용
1. **수중 파란색 혼합**: RGB에 파란색(6, 8, 255)을 20% 혼합
2. **고도 명도 조정**: elevation이 낮을수록 어둡게 (0.79배 등)

**결과**:
- 깊은 바다 (elevation=20): 어두운 파란색
- 얕은 해안 (elevation=55): 밝은 파란색
- 평지 (elevation=70): 원래 바이옴 색상

**트레이드오프**:
- 장점: 수심 정보 시각화
- 단점: 바이옴 본래 색상이 수중에서 변형되어 바이옴 구분 어려움

### 5.4. 픽셀 경계 정렬 문제

**문제**: 카메라가 `WORLD_SIZE_PER_PIXEL` 단위가 아닌 임의 위치에 있으면 픽셀과 월드 좌표가 정렬되지 않아 미세하게 떨림.

**해결**: `CalcOffsetPos()`로 offset position을 픽셀 경계에 정렬
```cpp
Vector3 CalcOffsetPos(Vector3 position, int gridSize) {
    int x = (int)(floor(position.x / gridSize)) * gridSize;
    int z = (int)(floor(position.z / gridSize)) * gridSize;
    return Vector3(x, 0, z);
}
```

**결과**: 카메라가 gridSize 이상 이동할 때만 offset position 변경 → 픽셀 경계와 일치

**트레이드오프**:
- 장점: 떨림 제거, 깔끔한 스크롤링
- 단점: 카메라가 gridSize 미만으로 이동할 때는 맵이 업데이트되지 않음 (정보 지연)

### 5.5. Climate Map의 메모리 크기

**문제**: CLIMATE_MAP_BUFFER_SIZE = CHUNK_COUNT × CHUNK_SIZE (예: 32 × 32 = 1024)이므로 Climate Map은 1024x1024 = 1,048,576 픽셀. `CLIMATE` 구조체가 8바이트(float 2개)라면 약 8MB 메모리 사용.

**해결**: Climate Map은 디버깅 용도로만 사용하므로 릴리스 빌드에서는 비활성화 가능 (조건부 컴파일).

**트레이드오프**:
- 장점: 블록 단위 정밀도로 온도/습도 시각화
- 단점: 메모리 오버헤드

## 6. 결과

### 6.1. 두 가지 맵 비교

| 항목           | Biome Map               | Climate Map                     |
|---------------|-------------------------|--------------------------------|
| 버퍼 크기      | 512x512 (262,144 픽셀)   | 1024x1024 (1,048,576 픽셀)      |
| 픽셀당 크기    | ~16 블록                 | 1 블록                          |
| 커버 범위      | ~8192 x 8192 블록        | 전체 렌더 거리                   |
| 데이터 타입    | RGBA_UINT (4 바이트)     | CLIMATE (8 바이트)              |
| 총 메모리      | 1 MB                     | 8 MB                            |
| 용도           | UI 미니맵 표시           | 디버깅, 셰이더 시각화            |
| 업데이트 빈도  | 카메라 이동 시            | 카메라 이동 시                   |

### 6.2. Scrolling 성능

**측정 결과 (추정)**:
- **초기화**: 512x512 = 262,144 픽셀 계산, 약 300ms
- **X축 이동**: 512 픽셀 계산, 약 0.6ms
- **대각선 이동**: 1024 픽셀 계산, 약 1.2ms
- **정지 상태**: 0ms (계산 없음)

**속도 향상**:
- 전체 재계산 대비 **256배** ~ **512배** 빠름
- 60 FPS 목표 (16.67ms/frame)에서 1~2ms는 충분히 허용 가능

### 6.3. 색상 인코딩 효과

**바이옴별 색상 예시**:
| 바이옴          | 기본 색상 (RGB)  | elevation=50 (수중) | elevation=70 (지상) | elevation=120 (산악) |
|----------------|-----------------|--------------------|--------------------|---------------------|
| Ocean          | (0, 0, 255)     | (1, 2, 255)        | (0, 0, 255)        | (0, 0, 255)         |
| Plains         | (0, 255, 0)     | (1, 204, 51)       | (0, 283, 0)        | (0, 255, 0)         |
| Desert         | (255, 200, 0)   | (204, 161, 51)     | (283, 222, 0)      | (255, 200, 0)       |
| Mountains      | (128, 128, 128) | (103, 103, 206)    | (142, 142, 142)    | (192, 192, 192)     |

**시각적 효과**:
- 수중 지형: 모든 바이옴이 파란색으로 변하여 물 표시
- 산악 지형: 밝은 색상으로 높이 강조
- 저지대: 어두운 색상으로 평지 표시

## 7. 회고

### 7.1. 아쉬운 점

1. **초기 로딩 시간**:
   - 512x512 픽셀 전체 계산에 수백 ms 소요
   - 게임 시작 시 맵 초기화로 인한 딜레이 발생
   - **개선 방향**: 멀티스레드로 맵 생성 병렬화 (청크 로딩과 동일하게 `std::async` 사용)

2. **픽셀당 노이즈 계산 중복**:
   - 동일한 월드 좌표의 노이즈를 WorldMap과 Chunk에서 각각 계산
   - Chunk 로딩 시 이미 계산한 continentalness, erosion 등을 WorldMap에서 재사용하지 못함
   - **개선 방향**: Terrain 노이즈 캐싱 시스템 도입 (LRU 캐시)

3. **수중 색상 보정의 단순함**:
   - elevation < 64 조건만으로 수중/지상 구분
   - 실제로는 바이옴이 Ocean이어도 elevation > 64이면 지상 색상 적용 (섬)
   - **개선 방향**: 바이옴 타입도 함께 고려하여 Ocean 바이옴은 항상 파란색 계열 유지

4. **Climate Map의 용도 제한**:
   - 현재는 디버깅 외에 활용도 없음
   - 8MB 메모리 사용하지만 UI에는 Biome Map만 표시
   - **개선 방향**: Climate Map을 셰이더에서 직접 샘플링하여 블록 색상 변화 적용 (온도별 풀 색상 등)

5. **고정된 맵 크기**:
   - BIOME_MAP_BUFFER_SIZE = 512 하드코딩
   - 화면 해상도나 렌더 거리에 따라 동적 조정 불가
   - **개선 방향**: 사용자 설정으로 맵 해상도 선택 가능 (256x256, 512x512, 1024x1024)

### 7.2. 다음에 개선하고 싶은 방향

1. **GPU Compute Shader 기반 맵 생성**:
   - CPU 노이즈 계산 대신 GPU Compute Shader로 병렬 계산
   - 512x512 픽셀을 16x16 스레드 그룹으로 병렬 처리 → 수십 배 빠른 생성
   - DirectCompute API 사용

2. **Mipmap 기반 Multi-scale 맵**:
   - 확대/축소 시 서로 다른 해상도 맵 표시
   - Zoom Out: 낮은 해상도, 넓은 범위
   - Zoom In: 높은 해상도, 좁은 범위
   - GPU Mipmap 자동 생성 활용

3. **실시간 바이옴 변화 표시**:
   - 계절 시스템 도입 시 온도 변화에 따라 바이옴 색상 동적 변경
   - Winter: Plains가 흰색(눈), Summer: 원래 녹색
   - 맵 색상도 실시간 갱신

4. **POI (Point of Interest) 마커**:
   - 플레이어가 방문한 위치, 건물, 랜드마크 등을 맵에 아이콘으로 표시
   - `std::vector<POI>` 저장 후 셰이더에서 오버레이

5. **3D 지형 맵 (Height Map)**:
   - 2D 평면 맵 대신 elevation을 Z축으로 표현
   - 3D 메쉬로 월드 지형 미니어처 렌더링
   - 카메라 회전 가능한 인터랙티브 맵
