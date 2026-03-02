## 프로젝트 개요

Voxen은 마인크래프트 모작의 오픈 월드 렌더링 프로젝트로, DirectX 11으로. MSAA를 적용한 디퍼드 셰이딩, 캐스케이드 섀도우 매핑, SSAO, PBR 라이팅, 멀티스레드 청크 관리 기반의 실시간 월드 생성 등 cpu/gpu 기술을 활용하여 구현한 프로젝트입니다.

- **개발 인원**
  - 개인 프로젝트
- **개발 기간**
  - `1차`: 2024.04 ~ 2025.01 (기본 렌더링 구현)
  - `2차`: 2025.07 ~ 2026.01 (청크 패치 및 보완점 수정)

## 빌드 시스템

이 프로젝트는 Visual Studio 2022를 사용하여 빌드합니다.

**빌드 방법:**

- Visual Studio 2022에서 `voxen.sln`을 열고 release x64 빌드
- 빌드 시 `release/` 폴더가 생성되며, 실행에 필요한 파일이 자동으로 배치됩니다
- 빌드를 진행하지 않고 `release/` 폴더에 `voxen.exe`가 존재합니다

**실행 방법:**

`release\voxen.exe` 실행

**`release/` 폴더 구조:**

```
release/
├── voxen.exe        # 실행 파일
├── assets/          # 텍스쳐, 모델 등 리소스
├── shaders/         # HLSL 쉐이더 파일
└── imgui.ini        # GUI 설정
```

`assets/`와 `shaders/`가 실행 파일과 같은 디렉터리에 배치되어 별도의 경로 설정 없이 바로 실행할 수 있습니다.

## 코드 구성

### 핵심 아키텍처

**진입점:**

- `voxen/srcs/main.cpp` - App 클래스를 초기화하고 실행

**메인 애플리케이션 루프:**

- `App` 클래스 (`App.h/cpp`) - 윈도우, DirectX 초기화, 렌더 루프를 관리하는 메인 애플리케이션 클래스
  - 모든 서브시스템 초기화 (Window, DirectX, GUI, Scene)
  - 매 프레임 ~60 FPS 목표로 업데이트 및 렌더링
  - 입력(키보드/마우스) 및 커서 잠금 관리

**그래픽스 코어:**

- `Graphics` 네임스페이스 (`Graphics.h/cpp`) - 중앙 그래픽스 관리 싱글톤
  - DirectX 11 장치, 컨텍스트, 스왑 체인 초기화
  - 모든 렌더 타겟, 깊이 버퍼, 셰이더 리소스 생성 및 관리
  - 각 렌더 패스별 파이프라인 상태 객체(PSO) 정의
  - 모든 GPU 리소스가 이 네임스페이스에 외부 정의됨

**렌더링 파이프라인:**
디퍼드 렌더링 파이프라인을 사용하며, 다음과 같은 주요 패스로 구성됩니다:

1. **섀도우 맵 패스** - 3단계 캐스케이드 섀도우 매핑
2. **G-Buffer 채우기** - Position, Normal, Albedo, Coverage, MER (Metallic/Emission/Roughness)
3. **MSAA 엣지 마스킹** - MSAA가 필요한 엣지 감지
4. **SSAO 렌더링** - 스크린 스페이스 앰비언트 오클루전
5. **디퍼드 셰이딩** - G-Buffer와 라이팅(PBR 기반) 결합
6. **포워드 렌더링** - 스카이박스, 구름, 투명체, 수면
7. **후처리** - 블룸, 톤 매핑

### 청크 시스템

**ChunkManager (싱글톤):**

- `ChunkManager` 클래스 (`ChunkManager.h/cpp`) - 월드의 모든 청크를 관리
  - 청크 할당에 오브젝트 풀링 사용 (`CHUNK_POOL_SIZE`)
  - 청크 크기: 32x32x32 블록 (`Chunk::CHUNK_SIZE`)
  - `std::future`를 통한 멀티스레드 로딩 및 패칭
  - 청크 생명주기 관리: 로드, 업데이트, 패치, 렌더, 언로드
  - 청크에 대한 프러스텀 컬링 구현
  - `m_patchDependencyMap`을 통한 청크 패치 의존성 추적

**청크 구조:**

- `Chunk` 클래스 (`Chunk.h/cpp`) - 개별 32x32x32 블록 영역
  - 패딩이 포함된 3D 배열에 블록 저장: `m_blocks[CHUNK_SIZE_P][CHUNK_SIZE_P][CHUNK_SIZE_P]`
  - 바이너리 그리디 메싱 알고리즘으로 메시 버텍스 생성
  - 지오메트리를 불투명, 반투명(Semi-Alpha), 투명, Low-LOD로 분리
  - 인스턴스(잔디, 꽃, 덩굴)를 `m_instanceMap`에 저장
  - 월드 생성은 `Initialize()` 메서드에서 수행

**메시 최적화:**

- 효율적인 버텍스 생성을 위한 바이너리 그리디 메싱 알고리즘
- 메모리 사용량 절감을 위한 비트 패킹 형식의 블록 데이터 저장
- 알파 모드별 분리된 버텍스/인덱스 버퍼

### 월드 생성

**지형 생성:**

- `Terrain` 클래스 (`Terrain.h`) - 노이즈 기반 지형 생성
  - 절차적 높이맵을 위한 FastNoise 라이브러리 사용
  - 지형 높이와 기본 구조 정의

**바이옴 시스템:**

- `Biome` 클래스 (`Biome.h/cpp`) - 바이옴 유형 결정
  - 다수의 바이옴 (평원, 사막, 사바나, 숲, 타이가, 툰드라, 늪, 바다)
  - 기후 기반(온도/습도) 바이옴 선택

**나무 생성:**

- `Tree` 클래스 (`Tree.h/cpp`) - 절차적 나무 배치 및 구조
  - 바이옴별 나무 종류 (참나무, 자작나무, 아카시아, 가문비나무, 눈가문비나무)
  - 나무가 여러 청크에 걸칠 수 있어 패치 전파가 필요

**블록 시스템:**

- `Block` 클래스 (`Block.h/cpp`) - 개별 블록 데이터
  - 블록 타입, 면 가시성 플래그, AO 값을 비트 패킹 형식으로 저장
  - `BLOCK_TYPE` 열거형에 50종 이상의 블록 타입 정의

### 주요 서브시스템

**카메라:**

- `Camera` 클래스 (`Camera.h/cpp`) - 프러스텀이 포함된 FPS 스타일 카메라
  - `MAX_RENDER_DISTANCE`로 청크 로딩 반경 결정
  - 뷰/프로젝션 행렬 업데이트
  - 프러스텀 컬링에 사용

**라이팅:**

- `Light` 클래스 (`Light.h/cpp`) - 캐스케이드 섀도우 맵이 포함된 방향광
  - `CASCADE_NUM = 3`개의 섀도우 캐스케이드
  - 각 캐스케이드의 섀도우 행렬 업데이트

**셰이더 관리:**

- 셰이더 위치: `voxen/shaders/`
- 공통 셰이더 코드: `Common.hlsli`
- 주요 셰이더: `BasicVS/PS.hlsl`, `ShadingBasicPS.hlsl`, `SkyboxVS/PS.hlsl`, `SsaoPS.hlsl`
- 섀도우 매핑: `ShadowGS.hlsl`이 지오메트리를 3개 캐스케이드로 확장

**후처리:**

- `PostEffect` 클래스 (`PostEffect.h/cpp`) - 블룸과 톤 매핑
  - 멀티 패스 블룸 다운샘플링/업샘플링
  - 리니어 톤 매핑

**수면 렌더링:**

- 미러 렌더 패스를 이용한 평면 반사
- 깊이 기반 투명도와 굴절
- 수중 필터링 효과

**스카이박스와 구름:**

- `Skybox` 클래스 - 큐브맵 없이 셰이더만으로 구현한 절차적 하늘, 동적 낮/밤 주기
- `Cloud` 클래스 - 노이즈 기반 구름 메시 생성과 투명 렌더링

## 개발 패턴

**싱글톤 패턴:**
많은 핵심 시스템이 정적 싱글톤 패턴을 사용합니다:

- `ChunkManager::GetInstance()`
- Graphics 네임스페이스 (정적 멤버)

**리소스 관리:**

- DirectX 리소스는 `ComPtr<>` 스마트 포인터 사용
- GPU 버퍼는 Graphics 네임스페이스에서 중앙 관리
- 효율적인 메모리 재사용을 위한 청크 오브젝트 풀링

**멀티스레딩:**

- 청크 로딩은 스레드 풀 패턴의 `std::future` 사용
- 패칭 시스템은 비동기로 실행
- 최대 스레드 수는 `m_initThreadCount`와 `m_patchThreadCount`로 제어

**데이터 구성:**

- 헤더: `voxen/headers/`
- 소스 파일: `voxen/srcs/`
- 에셋: `voxen/assets/` (텍스처, 모델)
- 셰이더: `voxen/shaders/` (HLSL)

**좌표계:**

- 월드 공간: 절대 블록 위치
- 청크 공간: 청크 원점 기준 상대 위치 (각 축 0-31)
- 청크 좌표: 월드 좌표를 청크 단위로 변환 (world_pos / CHUNK_SIZE)

## 중요 제약 사항

**청크 패칭:**
청크 경계에서 블록을 수정하면 인접 청크도 패치해야 합니다. 이 시스템은 다음을 사용합니다:

- `m_patchDependencyMap` - 청크 변경 시 업데이트가 필요한 청크 추적
- `PropagatePatchByEdgeBlock()` - 이웃 청크로 패치 전파
- 엣지 블록(로컬 좌표 0 또는 31)이 이웃 패치를 트리거

**블록 가시성:**

- 블록은 가려진 면을 컬링하기 위해 면 가시성을 저장
- 앰비언트 오클루전(AO) 값은 메시 생성 중 계산
- 그리디 메싱이 같은 블록 타입의 인접 면을 병합

**인스턴스 렌더링:**
잔디, 꽃, 덩굴은 인스턴스 렌더링을 사용합니다:

- `MeshGenerator` 네임스페이스에서 메시 생성
- 청크별 인스턴스 데이터를 `m_instanceMap`에 저장
- `RenderInstance()`로 인스턴싱 렌더링

**메모리 고려사항:**

- 청크 풀 크기가 고정됨 (`CHUNK_POOL_SIZE`)
- 인스턴스 버퍼 크기가 제한됨 (`MAX_INSTANCE_BUFFER_SIZE`)
- 렌더 거리를 초과한 청크는 메모리 해제를 위해 언로드

**섀도우 매핑:**

- 서로 다른 해상도의 3단계 캐스케이드 섀도우 맵
- 지오메트리 셰이더가 지오메트리를 3개 렌더 타겟으로 복제
- Light 클래스에서 섀도우 행렬 계산

## 그래픽스 파이프라인 세부사항

**G-Buffer 레이아웃:**

1. Position 버퍼 (RGB: 월드 위치)
2. Normal-Edge 버퍼 (RGB: 법선, A: 엣지 마스크)
3. Albedo 버퍼 (RGB: 기본 색상)
4. Coverage 버퍼 (A: MSAA용 커버리지 마스크)
5. MER 버퍼 (R: 메탈릭, G: 에미션, B: 러프니스)

**MSAA 전략:**

- 전체 화면 MSAA는 비용이 크므로 엣지 감지를 사용
- 엣지 픽셀만 MSAA(4x 샘플)로 셰이딩
- 비엣지 픽셀은 표준 단일 샘플 셰이딩

**텍스처 아틀라스:**

- 블록 텍스처를 아틀라스에 저장 (`blockAtlasMap`)
- 노멀 맵을 별도 아틀라스에 저장 (`normalAtlasMap`)
- MER 맵(메탈릭/에미션/러프니스)을 세 번째 아틀라스에 저장 (`merAtlasMap`)
- 잔디/잎/수면 바이옴 틴팅을 위한 컬러 맵

## 일반적인 수정 패턴

**새 블록 타입 추가:**

1. `Enums.h`의 `BLOCK_TYPE`에 열거형 값 추가
2. `Block.cpp`에서 블록 속성 설정 (투명, 반투명 등)
3. 관련 셰이더 또는 텍스처 아틀라스에 텍스처 좌표 추가

**렌더링 수정:**

1. `voxen/shaders/`의 관련 셰이더 업데이트
2. 셰이더 재컴파일 명령어 불필요 - 셰이더는 런타임에 로드됨
3. 변경사항 확인을 위해 애플리케이션 재시작

**청크 로딩 조정:**

1. `Camera::MAX_RENDER_DISTANCE`를 수정하여 시야 거리 변경
2. `ChunkManager::CHUNK_COUNT` 계산을 그에 맞춰 업데이트
3. `CHUNK_POOL_SIZE`의 메모리 영향 고려

**성능 튜닝:**

- SSAO 품질: `SsaoPS.hlsl`의 샘플 수 조정
- 섀도우 품질: `App.h`의 `SHADOW_WIDTH/HEIGHT` 수정
- 청크 로딩: ChunkManager의 스레드 수 튜닝

## 문서

개별 기능에 대한 상세 문서는 `docs/`에 위치합니다:

- `docs/gpu/` - GPU 측 렌더링 기술
- `docs/cpu/` - CPU 측 시스템 (청크 관리, 월드 생성, 메시 최적화)

각 하위 디렉터리에는 특정 기능에 대한 기술 설명과 다이어그램이 포함되어 있습니다.

---

---

# 그래픽 파이프라인 상세 구조

`App::Render()` 함수의 실제 호출 순서와 각 패스의 입출력을 기반으로 작성한 전체 렌더링 파이프라인입니다.

## 전체 흐름 개요

```
App::Render()
│
├── 1. Shadow Map Pass
├── 2. Deferred Rendering Pass
│     ├── G-Buffer Fill
│     ├── MSAA Edge Masking
│     ├── SSAO
│     └── Deferred Shading (PBR)
├── 3. ConvertToMSAA
├── 4. Forward Rendering Pass (수면 위/아래 분기)
├── 5. Post Effect (Water Filter + Bloom + Tone Mapping)
└── 6. UI (Biome Map)
```

## 상세 파이프라인

### Pass 1: Shadow Map

```
┌─────────────────────────────────────────────────────────────────┐
│  RenderShadowMap()                                              │
│                                                                 │
│  입력: 월드 지오메트리 (Opaque + Instance)                         │
│  출력: shadowDSV (Depth Buffer)                                  │
│                                                                 │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │  Cascade 0 (1024px)  │  Cascade 1 (1024px)  │  Cascade 2  │  │
│  │  near: 0~1.5%        │  mid: 1.5~3.5%       │  far: 3.5~15%│  │
│  │  대각선: 30           │  대각선: 88           │  대각선: 337  │  │
│  └───────────────────────────────────────────────────────────┘  │
│                                                                 │
│  ShadowGS.hlsl: 1개 삼각형 → 3개 캐스케이드로 Geometry Shader 복제   │
│  Light::Update(): 태양 궤도 회전 + Radiance 색상/세기 + 캐스케이드 행렬│
│                                                                 │
│  출력 → shadowSRV (ShadingBasicPS, WaterPlanePS에서 참조)          │
└─────────────────────────────────────────────────────────────────┘
```

### Pass 2: Deferred Rendering

#### 2-1. G-Buffer Fill

```
┌─────────────────────────────────────────────────────────────────┐
│  FillGBuffer()                                                  │
│                                                                 │
│  입력: 월드 지오메트리 + 텍스처 아틀라스 3종 + Climate Map            │
│  PSO: basicPSO (Opaque) / semiAlphaPSO (Semi-Alpha)             │
│  셰이더: BasicVS.hlsl → BasicPS.hlsl                             │
│                                                                 │
│  BasicVS: 비트 패킹된 정점 → 월드 좌표 + UV + 면 법선 디코딩          │
│  BasicPS: Normal Mapping(TBN) + Climate Color + MER 샘플링         │
│                                                                 │
│  ┌──────────── G-Buffer (MSAA 4x) ────────────┐                 │
│  │                                             │                │
│  │  RT0: NormalEdge  (R16G16B16A16_FLOAT)      │                │
│  │       RGB = World Normal (TBN 변환 후)       │                │
│  │       A   = Edge Flag (SV_COVERAGE 기반)     │                │
│  │                                             │                │
│  │  RT1: Position    (R16G16B16A16_FLOAT)      │                │
│  │       RGB = World Position                  │                │
│  │       A   = 유효성 플래그 (-1.0 = 빈 공간)     │                │
│  │                                             │                │
│  │  RT2: Albedo      (R16G16B16A16_FLOAT)      │                │
│  │       RGB = Base Color (Climate 틴팅 적용)    │                │
│  │                                             │                │
│  │  RT3: Coverage    (UINT)                    │                │
│  │       SV_COVERAGE 비트마스크 (4샘플)           │                │
│  │                                             │                │
│  │  RT4: MER         (R16G16B16A16_FLOAT)      │                │
│  │       R = Metallic                          │                │
│  │       G = Emission                          │                │
│  │       B = Roughness                         │                │
│  │                                             │                │
│  └─────────────────────────────────────────────┘                │
│                                                                 │
│  텍스처 입력:                                                     │
│    t0: blockAtlasTextureArray  — Albedo                         │
│    t1: normalAtlasTextureArray — Normal Map (Tangent Space)     │
│    t2: merAtlasTextureArray    — Metallic/Emission/Roughness    │
│    t3: grassColorMap           — 기후 기반 잔디 색상 LUT            │
│    t4: foliageColorMap         — 기후 기반 잎 색상 LUT              │
│    t5: climateNoiseMap         — 온도/습도 노이즈 텍스처             │
└─────────────────────────────────────────────────────────────────┘
```

#### 2-2. MSAA Edge Masking

```
┌─────────────────────────────────────────────────────────────────┐
│  MaskMSAAEdge()                                                 │
│                                                                 │
│  입력: G-Buffer (NormalEdge, Position, Coverage)                 │
│  PSO: edgeMaskingPSO (stencilMaskDSS, stencilRef=1)             │
│  셰이더: EdgeMaskingPS.hlsl                                      │
│                                                                 │
│  3단계 엣지 필터:                                                  │
│    1. Coverage 분석 → 4샘플 중 서로 다른 기하를 덮는 샘플 식별         │
│    2. 유효 샘플 검사 → position.w == -1.0 제외                      │
│    3. 법선/위치 차이 비교 → 임계값 초과 시 엣지 판정                   │
│                                                                 │
│  출력: Stencil Buffer에 엣지 영역 = 1 기록                         │
│                                                                 │
│  ┌─────────────────────────────────────────┐                    │
│  │  Stencil = 0: Non-Edge (대부분의 픽셀)     │                    │
│  │  Stencil = 1: Edge (블록 모서리, 실루엣)    │                    │
│  └─────────────────────────────────────────┘                    │
└─────────────────────────────────────────────────────────────────┘
```

#### 2-3. SSAO

```
┌─────────────────────────────────────────────────────────────────┐
│  RenderSSAO()                                                   │
│                                                                 │
│  입력: G-Buffer (NormalEdge, Position, Coverage)                 │
│  출력: ssaoRTV → ssaoSRV (R 채널, 차폐도)                         │
│                                                                 │
│  ┌─────── Non-Edge (ssaoPSO, Stencil=0) ──────┐                │
│  │  SsaoPS::main()                             │                │
│  │  샘플 0만 사용, 1회 getOcclusionFactor()       │                │
│  └─────────────────────────────────────────────┘                │
│  ┌─────── Edge (ssaoEdgePSO, Stencil=1) ──────┐                │
│  │  SsaoPS::mainMSAA()                         │                │
│  │  Coverage 가중치별 개별 getOcclusionFactor()   │                │
│  └─────────────────────────────────────────────┘                │
│                                                                 │
│  getOcclusionFactor() 핵심 과정:                                  │
│    texcoord → frac(texcoord×appSize/2)×3 → 4×4 노이즈 인덱스       │
│    → Bilinear 보간 → randomVec                                   │
│    → Gram-Schmidt → TBN (법선 방향 반구)                           │
│    → 16 samples × (View Space 투영 → 깊이 비교 → 차폐 판정)         │
│    → occlusionFactor / 16 → 거리 감쇠                             │
│                                                                 │
│  후처리: Blur 2회 (Separable Gaussian)                            │
│                                                                 │
│  최종: 1.0 - (occlusionFactor × attenuation)                     │
│        1.0 = 차폐 없음 (밝음), 0.0 = 완전 차폐 (어두움)              │
└─────────────────────────────────────────────────────────────────┘
```

#### 2-4. Deferred Shading (PBR)

```
┌─────────────────────────────────────────────────────────────────┐
│  ShadingBasic()                                                 │
│                                                                 │
│  입력: G-Buffer 전체 + ssaoSRV + shadowSRV + brdfSRV             │
│  출력: basicMSRTV (R16G16B16A16_FLOAT, HDR)                     │
│                                                                 │
│  ┌─────── Non-Edge (shadingBasicPSO, Stencil=0) ───────┐       │
│  │  ShadingBasicPS::main()                               │       │
│  │  4개 MSAA 샘플 평균 → 1회 라이팅 계산                    │       │
│  └───────────────────────────────────────────────────────┘       │
│  ┌─────── Edge (shadingBasicEdgePSO, Stencil=1) ───────┐       │
│  │  ShadingBasicPS::mainMSAA()                           │       │
│  │  각 샘플별 개별 라이팅 → 유효 샘플 평균                    │       │
│  └───────────────────────────────────────────────────────┘       │
│                                                                 │
│  ┌─────── PBR 라이팅 구조 ──────────────────────────────┐       │
│  │                                                       │       │
│  │  Ambient Lighting (IBL 근사):                          │       │
│  │    ├─ Diffuse: kd × albedo × (NdotL×radiance + ambient)│       │
│  │    └─ Specular: specIrradiance × (brdfLUT.r×F0 + .g)   │       │
│  │       └─ specIrradiance = lerp(reflectRad, ambient, rough)│    │
│  │                                                       │       │
│  │  Direct Lighting (Cook-Torrance BRDF):                │       │
│  │    ├─ F: Schlick Fresnel (Epic 최적화, pow(2,...))       │       │
│  │    ├─ D: GGX NDF (roughness² = alpha)                  │       │
│  │    ├─ G: Schlick-GGX Geometry (k=(r+1)²/8)             │       │
│  │    ├─ Specular: (F×D×G) / (4×NdotI×NdotO)              │       │
│  │    ├─ Diffuse: kd × albedo                              │       │
│  │    └─ × radianceWeight × radianceColor × shadow × NdotI │       │
│  │                                                       │       │
│  │  ao = pow(ssao, 4.0)   ← SSAO 강화                     │       │
│  │  shadow = pow(factor, 3.0) ← 그림자 경계 선명화           │       │
│  │  출력: clamp(ambient + direct, 0, 1000)  ← HDR 유지      │       │
│  │                                                       │       │
│  └───────────────────────────────────────────────────────┘       │
│                                                                 │
│  Light.cpp에서 매 프레임 갱신:                                     │
│    태양 방향: 45° 기울어진 축으로 궤도 회전 (24000틱 = 1주기)          │
│    Radiance 세기: Smootherstep(고도) → [0, 2.0]                  │
│    Radiance 색상: Day(흰) → Sunset(주황) → Night(검정) → Sunrise(금)│
│    sRGB → Linear 변환 후 GPU 전달                                 │
└─────────────────────────────────────────────────────────────────┘
```

### Pass 3: ConvertToMSAA

```
┌─────────────────────────────────────────────────────────────────┐
│  ConvertToMSAA()                                                │
│                                                                 │
│  Non-MSAA 셰이딩 결과(basicRTV)를 MSAA 텍스처(basicMSRTV)로 복사    │
│  → Forward 패스에서 MSAA 렌더 타겟에 직접 그리기 위함                  │
│                                                                 │
│  basicRTV (Non-MSAA) ──→ basicMSRTV (MSAA 4x)                  │
└─────────────────────────────────────────────────────────────────┘
```

### Pass 4: Forward Rendering (수면 위/아래 분기)

```
┌─────────────────────────────────────────────────────────────────┐
│  Forward Render Pass                                            │
│                                                                 │
│  출력 대상: basicMSRTV (MSAA 4x, HDR)                            │
│                                                                 │
│  ┌──────────── 수면 위 (IsUnderWater == false) ────────────┐    │
│  │                                                         │    │
│  │  ① RenderMirrorWorld()     ← 수면 반사 텍스처 준비         │    │
│  │     ├─ Stencil Masking     수면 영역 표시 + Mirror Depth   │    │
│  │     ├─ Mirror Sky          반전 카메라로 하늘 렌더링         │    │
│  │     ├─ Mirror Cloud        반전 카메라 + 와인딩 반전         │    │
│  │     ├─ Mirror Block        반전 카메라 + Low-LOD 블록       │    │
│  │     └─ Blur (3회)          잔물결에 의한 반사 흐림           │    │
│  │     출력: mirrorWorldSRV (절반 해상도, R16G16B16A16_FLOAT) │    │
│  │                                                         │    │
│  │  ② RenderWaterPlane()      ← 수면 최종 합성               │    │
│  │     ├─ Beer-Lambert 흡수:                                │    │
│  │     │    projColor = lerp(origin, waterAlbedo, absorption)│    │
│  │     │    absorption = 1 - exp(-0.075 × 수중통과거리)        │    │
│  │     ├─ Fresnel 블렌딩:                                    │    │
│  │     │    blendColor = lerp(projColor×(1-F), mirror, F)    │    │
│  │     │    F0 = 0.2 (수면 기본 반사율)                        │    │
│  │     └─ 입력: originColor, mirrorColor, positionTex,       │    │
│  │              waterColorMap, climateNoise, waterStillAtlas  │    │
│  │                                                         │    │
│  │  ③ RenderFogFilter()       ← 안개                       │    │
│  │  ④ RenderSkybox()          ← 동적 하늘                   │    │
│  │  ⑤ RenderCloud()           ← 구름                       │    │
│  │                                                         │    │
│  └─────────────────────────────────────────────────────────┘    │
│                                                                 │
│  ┌──────────── 수중 (IsUnderWater == true) ────────────────┐    │
│  │                                                         │    │
│  │  ① RenderFogFilter()       ← 수중 안개 (가시거리 단축)     │    │
│  │     fogDistMin: 15→30,  fogDistMax: 30→120 (적응에 따라)  │    │
│  │                                                         │    │
│  │  ② RenderSkybox()          ← 하늘                       │    │
│  │  ③ RenderCloud()           ← 구름                       │    │
│  │                                                         │    │
│  │  ④ RenderWaterPlane()      ← 수면 (위에서 덮음)           │    │
│  │     수중: lerp(originColor, waterColor, 0.5)             │    │
│  │     Mirror 없음, Fresnel 없음                             │    │
│  │                                                         │    │
│  └─────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────┘
```

### Pass 5: Post Effect

```
┌─────────────────────────────────────────────────────────────────┐
│  Post Effect                                                    │
│                                                                 │
│  ResolveSubresource: basicMSBuffer(MSAA) → basicBuffer(Non-MSAA)│
│                                                                 │
│  ┌─── 수중 전용: RenderWaterFilter() ──────────────────────┐    │
│  │  filterColor × clamp(radianceWeight, 0.1, 1.0)          │    │
│  │  적응 시간(0~2.5초)에 따라:                                │    │
│  │    filterStrength: 0.9 → 0.4 (시야 점진 확보)             │    │
│  │    filterColor: 연한 청색 → 짙은 청색                      │    │
│  └─────────────────────────────────────────────────────────┘    │
│                                                                 │
│  ┌─── Bloom ───────────────────────────────────────────────┐    │
│  │                                                         │    │
│  │  Down (13-tap 가중 필터, 텐트 분포):                       │    │
│  │    basicSRV ──→ bloom[1] (1/2) ──→ bloom[2] (1/4)       │    │
│  │              ──→ bloom[3] (1/8) ──→ bloom[4] (1/16)     │    │
│  │                                                         │    │
│  │  Up (3×3 텐트 필터 + 하드웨어 Bilinear):                   │    │
│  │    bloom[4] ──→ bloom[3] ──→ bloom[2]                   │    │
│  │             ──→ bloom[1] ──→ bloom[0] (원본 해상도)       │    │
│  │                                                         │    │
│  │  Combine & Tone Mapping:                                │    │
│  │    ├─ HG Phase Function: 태양 방향 → Bloom 강도 보너스     │    │
│  │    │    bloomStrength = HG(lightDir, eyeDir, 0.9)×0.25   │    │
│  │    │                   + (수중 ? 0.25 : 0.1)             │    │
│  │    ├─ 합성: lerp(renderColor, bloomColor, bloomStrength)  │    │
│  │    └─ Linear Tone Mapping:                              │    │
│  │         clamp(exposure × color, 0, 1)                   │    │
│  │         pow(color, 1/2.2)   ← 감마 보정 (Linear → sRGB)  │    │
│  │                                                         │    │
│  │  출력: backBufferRTV (LDR, 디스플레이)                     │    │
│  └─────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────┘
```

## 버퍼 흐름도

```
┌────────────┐
│ Shadow DSV │ ─────────────────────────────────────────────────┐
└────────────┘                                                  │
                                                                ▼
┌──────────────────────── G-Buffer (MSAA 4x) ──────────────────────────┐
│  NormalEdge │ Position │ Albedo │ Coverage │ MER                      │
└──────┬──────┴────┬─────┴───┬────┴────┬─────┴──┬─────────────────────┘
       │           │         │         │        │
       ▼           ▼         │         ▼        │
┌──────────┐ ┌──────────┐   │  ┌──────────┐    │
│ Edge     │ │ SSAO     │   │  │ Coverage │    │
│ Masking  │ │          │   │  │ Analysis │    │
│ →Stencil │ │ →ssaoSRV │   │  │          │    │
└──────────┘ └────┬─────┘   │  └──────────┘    │
                  │         │                   │
                  ▼         ▼                   ▼
          ┌───────────────────────────────────────────┐
          │         Deferred Shading (PBR)             │
          │  Ambient (IBL 근사) + Direct (Cook-Torrance)│◄── shadowSRV
          │  + SSAO + Shadow                           │◄── brdfSRV
          │                                            │
          │  → basicMSRTV (HDR, MSAA 4x)               │
          └─────────────────┬──────────────────────────┘
                            │
                   ConvertToMSAA
                            │
                            ▼
          ┌───────────────────────────────────────────┐
          │         Forward Rendering                  │
          │                                            │
          │  Mirror World → Water Plane                │
          │  Fog Filter → Skybox → Cloud               │
          │                                            │
          │  → basicMSRTV (HDR, MSAA 4x)               │
          └─────────────────┬──────────────────────────┘
                            │
                   ResolveSubresource
                   (MSAA → Non-MSAA)
                            │
                            ▼
          ┌───────────────────────────────────────────┐
          │         Post Effect                        │
          │                                            │
          │  [수중] Water Filter                        │
          │  Bloom Down (4단계) → Bloom Up (4단계)       │
          │  Combine (HG Phase) + Tone Mapping (1/2.2) │
          │                                            │
          │  → backBufferRTV (LDR, 디스플레이)            │
          └────────────────────────────────────────────┘
```

## CPU 파이프라인 — 청크가 렌더링되기까지

`ChunkManager::Update()`의 실제 호출 순서와 `Chunk::Initialize()`의 내부 과정을 기반으로 작성한, 청크가 생성되어 GPU에 제출되기까지의 전체 CPU 측 흐름입니다.

### 전체 흐름 개요

```
ChunkManager::Update()
│
├── 1. UpdateChunkList()        — 카메라 이동 시 로드/언로드 대상 결정
├── 2. UpdateLoadChunkList()    — 비동기 로드 (월드 생성 + 메싱)
├── 3. UpdateUnloadChunkList()  — 범위 밖 청크 해제
├── 4. UpdatePatchChunkMap()    — 비동기 패치 (경계 블록 수정)
├── 5. UpdateRenderChunkList()  — 프러스텀 컬링 → 렌더 리스트 구성
├── 6. UpdateInstanceInfoList() — 인스턴스 버퍼 갱신
└── 7. UpdateChunkConstant()    — 청크 상수 버퍼 갱신
```

### Step 1: 청크 리스트 갱신 — UpdateChunkList()

```
┌─────────────────────────────────────────────────────────────────┐
│  UpdateChunkList(cameraChunkPos)                                │
│  트리거: 카메라가 청크 경계를 넘을 때 (m_isOnChunkDirtyFlag)        │
│                                                                 │
│  카메라 중심 기준 렌더 범위 내 청크 좌표 순회:                       │
│    X: CHUNK_COUNT개 (카메라 ± MAX_RENDER_DISTANCE)               │
│    Y: MAX_HEIGHT_CHUNK_COUNT (0~256, 고정 8층)                   │
│    Z: CHUNK_COUNT개                                             │
│                                                                 │
│  ┌──────────────────────────────────────────────┐               │
│  │  m_chunkMap에 없는 좌표?                       │               │
│  │    → 오브젝트 풀에서 Chunk* 할당               │               │
│  │    → m_chunkMap에 등록                         │               │
│  │    → m_loadChunkList에 추가 (로딩 대기)         │               │
│  │                                               │               │
│  │  m_chunkMap에 있지만 범위 밖?                    │               │
│  │    → m_unloadChunkList에 추가 (언로드 대기)     │               │
│  └──────────────────────────────────────────────┘               │
│                                                                 │
│  오브젝트 풀: 고정 크기 CHUNK_POOL_SIZE개의 Chunk 재사용            │
│  (new/delete 없이 Clear() 후 재할당)                              │
└─────────────────────────────────────────────────────────────────┘
```

### Step 2: 비동기 청크 로드 — UpdateLoadChunkList()

```
┌─────────────────────────────────────────────────────────────────┐
│  UpdateLoadChunkList(camera)                                    │
│                                                                 │
│  ① 카메라 거리 기준 정렬 (가까운 것부터 로드)                       │
│                                                                 │
│  ② 스레드 풀에 비동기 제출 (최대 m_initThreadCount개 동시)          │
│     std::async(Chunk::Initialize, chunk, chunkLoadMemory)       │
│                                                                 │
│     ┌─────────────────────────────────────────────────────┐     │
│     │  Chunk::Initialize() — 워커 스레드에서 실행            │     │
│     │                                                     │     │
│     │  ┌─── (a) 지형 노이즈 생성 ──────────────────────┐  │     │
│     │  │  InitTerrainNoises(memory)                     │  │     │
│     │  │                                               │  │     │
│     │  │  FastNoise 라이브러리로 7종 노이즈맵 생성:       │  │     │
│     │  │    ├─ continentaliness  (대륙성 — 해안/내륙)    │  │     │
│     │  │    ├─ erosion          (침식 — 지형 편평도)     │  │     │
│     │  │    ├─ peaksValley      (봉우리/계곡)           │  │     │
│     │  │    ├─ temperature      (온도 — 바이옴 결정)     │  │     │
│     │  │    ├─ humidity         (습도 — 바이옴 결정)     │  │     │
│     │  │    ├─ distribution     (분포 — 인스턴스 배치)   │  │     │
│     │  │    └─ elevation        (고도 — 세부 지형)       │  │     │
│     │  │                                               │  │     │
│     │  │  패딩 포함 34×34 영역 (인접 청크 경계 포함)       │  │     │
│     │  └───────────────────────────────────────────────┘  │     │
│     │                                                     │     │
│     │  ┌─── (b) 바이옴 결정 ───────────────────────────┐  │     │
│     │  │  InitBiomeMapAndCount(memory)                  │  │     │
│     │  │                                               │  │     │
│     │  │  temperature × humidity → Biome 유형 결정       │  │     │
│     │  │  (Plains, Desert, Forest, Taiga, Tundra,       │  │     │
│     │  │   Savanna, Swamp, Ocean)                       │  │     │
│     │  │                                               │  │     │
│     │  │  biomeMap2D[32×32] + biomeCount[8] 생성         │  │     │
│     │  └───────────────────────────────────────────────┘  │     │
│     │                                                     │     │
│     │  ┌─── (c) 블록 타입 결정 ────────────────────────┐  │     │
│     │  │  InitBasicBlockType(memory)                    │  │     │
│     │  │                                               │  │     │
│     │  │  노이즈 조합 → 지형 높이 결정                    │  │     │
│     │  │  높이 + 바이옴 → 블록 타입 결정:                 │  │     │
│     │  │    ├─ 표면: GRASS / SAND / SNOW / ...          │  │     │
│     │  │    ├─ 지하: STONE / DIRT / ...                 │  │     │
│     │  │    ├─ 수면: WATER (Y ≤ 64)                     │  │     │
│     │  │    └─ 공기: AIR                                │  │     │
│     │  │                                               │  │     │
│     │  │  m_blocks[34][34][34] 배열에 기록               │  │     │
│     │  │  (패딩 포함, 인접 청크 경계 블록 포함)             │  │     │
│     │  └───────────────────────────────────────────────┘  │     │
│     │                                                     │     │
│     │  ┌─── (d) 나무 배치 ─────────────────────────────┐  │     │
│     │  │  InitTreePlace(memory)                         │  │     │
│     │  │                                               │  │     │
│     │  │  해시 기반 의사 난수로 배치 후보 좌표 생성         │  │     │
│     │  │  바이옴별 최대 배치 수 제한                       │  │     │
│     │  │  바이옴 → 나무 종류 결정:                        │  │     │
│     │  │    Forest→Oak, Birch                           │  │     │
│     │  │    Taiga→Spruce    Tundra→SnowySpruces         │  │     │
│     │  │    Savanna→Acacia                              │  │     │
│     │  │                                               │  │     │
│     │  │  나무가 청크 경계를 넘으면:                       │  │     │
│     │  │    → chunkPatchDataMap에 패치 데이터 기록         │  │     │
│     │  │    → 로드 완료 후 인접 청크에 패치 전파            │  │     │
│     │  └───────────────────────────────────────────────┘  │     │
│     │                                                     │     │
│     │  ┌─── (e) 인스턴스 배치 ─────────────────────────┐  │     │
│     │  │  InitInstancePlace(memory)                     │  │     │
│     │  │                                               │  │     │
│     │  │  잔디, 꽃, 덩굴, 수초 등                        │  │     │
│     │  │  해시 기반 의사 난수 + 바이옴 조건으로 배치        │  │     │
│     │  │  → m_instanceMap에 저장                         │  │     │
│     │  └───────────────────────────────────────────────┘  │     │
│     │                                                     │     │
│     │  ┌─── (f) 메시 생성 (Binary Greedy Meshing) ─────┐  │     │
│     │  │  InitWorldVerticesData(memory)                  │  │     │
│     │  │                                               │  │     │
│     │  │  ① 면 컬링 비트 생성                            │  │     │
│     │  │     MakeFaceSliceColumnBit()                   │  │     │
│     │  │     각 블록의 6면에 대해, 인접 블록이 불투명이면   │  │     │
│     │  │     해당 면을 비트마스크에서 제거 (가려진 면 컬링)  │  │     │
│     │  │                                               │  │     │
│     │  │  ② 블록 타입별 슬라이스 비트 분류                 │  │     │
│     │  │     같은 블록 타입끼리 비트 열 그룹화              │  │     │
│     │  │                                               │  │     │
│     │  │  ③ Greedy Meshing                              │  │     │
│     │  │     64비트 열 단위로 비트 AND 연산 →              │  │     │
│     │  │     인접한 같은 타입의 면을 하나의 큰 사각형으로 병합│  │     │
│     │  │     → 버텍스/인덱스 수 대폭 감소                  │  │     │
│     │  │                                               │  │     │
│     │  │  ④ 알파 모드별 분리 출력:                        │  │     │
│     │  │     ├─ m_opaqueVertices/Indices   (불투명)      │  │     │
│     │  │     ├─ m_semiAlphaVertices/Indices (반투명)      │  │     │
│     │  │     ├─ m_transparencyVertices/Indices (투명=물)  │  │     │
│     │  │     └─ m_lowLodVertices/Indices    (원거리 LOD)  │  │     │
│     │  │                                               │  │     │
│     │  │  버텍스 포맷 (VoxelVertex):                     │  │     │
│     │  │     비트 패킹된 uint32_t 2개로 구성               │  │     │
│     │  │     (위치, 면 방향, 텍스처 인덱스, UV 등)          │  │     │
│     │  └───────────────────────────────────────────────┘  │     │
│     │                                                     │     │
│     │  반환: ChunkLoadMemory* (패치 의존성 데이터 포함)      │     │
│     └─────────────────────────────────────────────────────┘     │
│                                                                 │
│  ③ 완료된 Future 수확 (메인 스레드)                                │
│     ├─ 패치 의존성 맵(m_patchDependencyMap) 갱신                  │
│     │    나무 등이 인접 청크에 걸쳐 있으면, 해당 청크에 패치 예약     │
│     ├─ chunk->UpdateCpuBufferCount()  (버텍스/인덱스 카운트 확정)  │
│     ├─ UpdateChunkBuffer(chunk)       (GPU 버퍼 업로드)           │
│     ├─ chunk->SetLoad(true)                                     │
│     └─ ChunkLoadMemory 풀에 반환 (재사용)                         │
└─────────────────────────────────────────────────────────────────┘
```

### Step 3: 청크 언로드 — UpdateUnloadChunkList()

```
┌─────────────────────────────────────────────────────────────────┐
│  UpdateUnloadChunkList()                                        │
│                                                                 │
│  렌더 범위를 벗어난 청크 정리:                                      │
│    ├─ m_chunkMap에서 제거                                        │
│    ├─ m_patchChunkMap에서 제거                                   │
│    ├─ m_patchDependencyMap에서 제거 + 역참조 정리                  │
│    ├─ m_patchedChunkSet에서 제거                                 │
│    ├─ chunk->Clear() — 블록/인스턴스/버텍스 데이터 초기화            │
│    └─ ReleaseChunkToPool(chunk) — 오브젝트 풀로 반환               │
│                                                                 │
│  핵심: new/delete 없이 풀에서 재활용하여 메모리 할당 비용 제거        │
└─────────────────────────────────────────────────────────────────┘
```

### Step 4: 비동기 패치 — UpdatePatchChunkMap()

```
┌─────────────────────────────────────────────────────────────────┐
│  UpdatePatchChunkMap(camera)                                    │
│                                                                 │
│  패치 트리거:                                                     │
│    ├─ 나무가 인접 청크 경계를 넘었을 때 (로드 시 자동 발생)          │
│    ├─ 플레이어가 블록을 파괴/설치했을 때                            │
│    └─ 엣지 블록(좌표 0 또는 31)이 수정되었을 때                     │
│        → PropagatePatchByEdgeBlock()로 인접 청크에도 전파          │
│                                                                 │
│  ① 카메라 거리 기준 정렬 (가까운 것부터 패치)                       │
│                                                                 │
│  ② 스레드 풀에 비동기 제출 (최대 m_patchThreadCount개 동시)         │
│     std::async(Chunk::Patch, chunk, patchDataSet, memory)       │
│                                                                 │
│     ┌─────────────────────────────────────────────────────┐     │
│     │  Chunk::Patch() — 워커 스레드에서 실행                │     │
│     │                                                     │     │
│     │  ① PatchDataSet의 블록/인스턴스 변경 사항 적용          │     │
│     │     m_blocks[x][y][z] = newBlock                    │     │
│     │     m_instanceMap[pos] = newInstance                 │     │
│     │                                                     │     │
│     │  ② InitWorldVerticesData() — 메시 재생성              │     │
│     │     (Binary Greedy Meshing 전체 재실행)               │     │
│     │                                                     │     │
│     │  반환: ChunkLoadMemory*                              │     │
│     └─────────────────────────────────────────────────────┘     │
│                                                                 │
│  ③ 완료된 Future 수확 (메인 스레드)                                │
│     ├─ chunk->UpdateCpuBufferCount()                            │
│     ├─ OnPatchDirtyFlag → UpdateChunkBuffer() (GPU 버퍼 갱신)    │
│     └─ ChunkLoadMemory 풀에 반환                                 │
└─────────────────────────────────────────────────────────────────┘
```

### Step 5: 렌더 리스트 구성 — UpdateRenderChunkList()

```
┌─────────────────────────────────────────────────────────────────┐
│  UpdateRenderChunkList(camera, light)                           │
│                                                                 │
│  로드된 모든 청크(m_chunkMap)를 순회하며 프러스텀 컬링:              │
│                                                                 │
│  ┌──────────── FrustumCulling() ───────────────────────┐        │
│  │  ViewProj 역행렬 → 8개 프러스텀 꼭짓점 → 6개 평면      │        │
│  │  청크 AABB(32×32×32)의 8개 꼭짓점 vs 6개 평면 검사     │        │
│  │  → 모든 꼭짓점이 하나의 평면 바깥이면 컬링              │        │
│  └─────────────────────────────────────────────────────┘        │
│                                                                 │
│  3개의 렌더 리스트 구성:                                           │
│                                                                 │
│  ├─ m_renderChunkList       — 카메라 프러스텀 통과                │
│  │    → RenderBasic() (Opaque + SemiAlpha + LowLod + Instance) │
│  │    → RenderTransparency() (Water)                           │
│  │                                                             │
│  ├─ m_renderShadowChunkList — 3개 캐스케이드 프러스텀 중 하나 통과  │
│  │    → RenderBasicShadowMap() (LowLod만)                      │
│  │                                                             │
│  └─ m_renderMirrorChunkList — 반전 카메라 프러스텀 통과            │
│       → RenderMirrorWorld() (LowLod만)                         │
└─────────────────────────────────────────────────────────────────┘
```

### Step 6: GPU 버퍼 업로드 — UpdateChunkBuffer()

```
┌─────────────────────────────────────────────────────────────────┐
│  UpdateChunkBuffer(chunk) — 메인 스레드                          │
│                                                                 │
│  CPU 메모리의 버텍스/인덱스 → GPU D3D11 Buffer로 전송              │
│                                                                 │
│  청크 ID(풀 인덱스) 기반으로 사전 할당된 버퍼 슬롯 사용:             │
│                                                                 │
│  ┌─── 버퍼 종류 (청크당 최대 9개) ──────────────────────┐        │
│  │                                                     │        │
│  │  m_constantBuffers[id]           — World 행렬         │        │
│  │                                                     │        │
│  │  m_lowLodVertexBuffers[id]       — Low-LOD VB        │        │
│  │  m_lowLodIndexBuffers[id]        — Low-LOD IB        │        │
│  │                                                     │        │
│  │  m_opaqueVertexBuffers[id]       — Opaque VB         │        │
│  │  m_opaqueIndexBuffers[id]        — Opaque IB         │        │
│  │                                                     │        │
│  │  m_semiAlphaVertexBuffers[id]    — SemiAlpha VB      │        │
│  │  m_semiAlphaIndexBuffers[id]     — SemiAlpha IB      │        │
│  │                                                     │        │
│  │  m_transparencyVertexBuffers[id] — Transparency VB   │        │
│  │  m_transparencyIndexBuffers[id]  — Transparency IB   │        │
│  │                                                     │        │
│  └─────────────────────────────────────────────────────┘        │
│                                                                 │
│  ResizeBuffer(): 기존 버퍼가 작으면 재할당, 크면 재사용             │
│  UpdateBuffer(): D3D11 Map/Unmap으로 CPU → GPU 복사              │
└─────────────────────────────────────────────────────────────────┘
```

### Step 7: 렌더링 — RenderBasic() / DrawIndexed()

```
┌─────────────────────────────────────────────────────────────────┐
│  RenderBasic(cameraPos) — GPU 제출                              │
│                                                                 │
│  m_renderChunkList 순회:                                         │
│                                                                 │
│  ┌─── 거리 판정 ───────────────────────────────────────┐        │
│  │                                                     │        │
│  │  카메라~청크 거리 > LOD_RENDER_DISTANCE?              │        │
│  │    YES → basicPSO + RenderLowLodChunk()              │        │
│  │            (병합된 Low-LOD 메시, 디테일 생략)           │        │
│  │    NO  → basicPSO + RenderOpaqueChunk()              │        │
│  │         + semiAlphaPSO + RenderSemiAlphaChunk()      │        │
│  │            (풀 디테일 메시)                            │        │
│  │                                                     │        │
│  └─────────────────────────────────────────────────────┘        │
│                                                                 │
│  instancePSO + RenderInstance():                                │
│    4가지 셰이프(Cross, Fence, Square, Floor) ×                   │
│    DrawIndexedInstanced() (인스턴스당 1회 Draw Call)              │
│                                                                 │
│  각 Draw Call:                                                   │
│    IASetVertexBuffers(VB[chunk.id])                              │
│    IASetIndexBuffer(IB[chunk.id])                                │
│    VSSetConstantBuffers(CB[chunk.id])  — World 행렬              │
│    DrawIndexed(indexCount)                                       │
│                                                                 │
│  → GPU 파이프라인 (BasicVS → BasicPS → G-Buffer)으로 전달         │
└─────────────────────────────────────────────────────────────────┘
```

### 전체 흐름 요약

```
카메라 이동
    │
    ▼
UpdateChunkList ─── 범위 밖 청크 → UnloadList → Clear → 풀 반환
    │
    │ 범위 내 새 청크 → LoadList
    ▼
UpdateLoadChunkList
    │
    │ std::async (워커 스레드)
    ▼
┌─────────────────── Chunk::Initialize() ───────────────────┐
│                                                           │
│  노이즈 생성 (7종)                                         │
│       ↓                                                   │
│  바이옴 결정 (Temperature × Humidity)                       │
│       ↓                                                   │
│  블록 타입 결정 (높이 + 바이옴 → 50종+)                      │
│       ↓                                                   │
│  나무 배치 (바이옴별 종류, 경계 넘기 → 패치 데이터)             │
│       ↓                                                   │
│  인스턴스 배치 (잔디, 꽃, 덩굴)                               │
│       ↓                                                   │
│  Binary Greedy Meshing                                    │
│    ├─ 면 컬링 (인접 불투명 블록으로 가려진 면 제거)            │
│    ├─ 블록 타입별 비트 분류                                  │
│    ├─ 64비트 열 단위 그리디 병합                              │
│    └─ 알파 모드별 버텍스/인덱스 분리 출력                      │
│                                                           │
└──────────────────────────┬────────────────────────────────┘
                           │
                  메인 스레드로 반환
                           │
                           ▼
              패치 의존성 맵 갱신
              UpdateChunkBuffer() ─── CPU → GPU 버퍼 복사
              chunk.SetLoad(true)
                           │
                           ▼
              UpdatePatchChunkMap ─── 인접 청크 패치 (비동기)
                           │          (나무 경계, 블록 파괴/설치)
                           │          → Chunk::Patch() → 메시 재생성
                           ▼
              UpdateRenderChunkList ─── 프러스텀 컬링
                           │             ├─ 카메라 프러스텀
                           │             ├─ 섀도우 3 캐스케이드 프러스텀
                           │             └─ 미러 (반전) 프러스텀
                           ▼
              UpdateInstanceInfoList ─── LOD 거리 내 인스턴스 수집
                           │              → 인스턴스 Info 버퍼 갱신
                           ▼
              RenderBasic() ─── GPU 제출
                ├─ LOD 거리 판정 → LowLod / Opaque+SemiAlpha
                ├─ DrawIndexed() per chunk
                ├─ DrawIndexedInstanced() per shape
                └─ → GPU 파이프라인 (BasicVS/PS → G-Buffer → ...)
```

## 문서 매핑

각 파이프라인 단계에 대응하는 상세 문서 위치:

### GPU 파이프라인

| 단계                                 | 문서 경로                        |
| ------------------------------------ | -------------------------------- |
| Cascade Shadow Map                   | `docs/gpu/CascadeShadowMap/`     |
| G-Buffer Fill + MSAA 전략            | `docs/gpu/DeferredShading_MSAA/` |
| MSAA Bleeding / Mipmap 이슈          | `docs/gpu/MSAA_Issues/`          |
| SSAO                                 | `docs/gpu/SSAO/`                 |
| PBR Lighting (Ambient + Direct)      | `docs/gpu/Lighting/`             |
| Grass / Leaf Color (Climate)         | `docs/gpu/GrassLeafColor/`       |
| Water (Mirror + Absorption + Filter) | `docs/gpu/Water/`                |
| Fog                                  | `docs/gpu/Fog/`                  |
| Skybox (동적 하늘)                   | `docs/gpu/SkyBox/`               |
| Cloud                                | `docs/gpu/Cloud/`                |
| Frustum Culling                      | `docs/gpu/FrustumCulling/`       |
| Post Effect (Bloom + Tone Mapping)   | `docs/gpu/PostEffect/`           |

### CPU 파이프라인

| 단계                                  | 문서 경로                                        |
| ------------------------------------- | ------------------------------------------------ |
| 청크 생명주기 (Load / Unload / Patch) | `docs/cpu/ChunkManagement/ChunkLifecycle/`       |
| 청크 업데이트 (상수 버퍼 / 풀 관리)   | `docs/cpu/ChunkManagement/ChunkUpdate/`          |
| 바이너리 블록 정보 (비트 패킹)        | `docs/cpu/MeshOptimization/BinaryBlockInfo/`     |
| 바이너리 그리디 메싱                  | `docs/cpu/MeshOptimization/BinaryGreedyMeshing/` |
| 지형 생성 (노이즈 기반)               | `docs/cpu/WorldGeneration/Terrain/`              |
| 바이옴 결정 (온도 × 습도)             | `docs/cpu/WorldGeneration/Biome/`                |
| 나무 생성 (바이옴별 종류)             | `docs/cpu/WorldGeneration/Tree/`                 |
| 블록 타입 결정                        | `docs/cpu/WorldGeneration/BlockType/`            |
| 월드맵 (2D 미니맵)                    | `docs/cpu/WorldGeneration/WorldMap/`             |
