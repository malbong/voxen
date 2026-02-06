## 개요

- **Minecraft를 기반으로한 Voxel 오픈월드 렌더링 프로젝트**
- **개발 인원**
  - 개인 프로젝트
- **개발 기간**
  - `1차`: 2024.04 ~ 2025.01 (기본 렌더링 구현)
  - `2차`: 2025.07 ~ 2026.01 (청크 패치 및 보완점 수정)

<br />

## 결과

-

<br />

## 프로젝트 내용

-

<br />

## 파이프라인 도식화

-

<br />

## 시스템 구성요소 및 문서

### **GPU 관점**

**Cascade ShadowMap**

- 그림자 구현을 위한 쉐도우맵 구현

**MSAA Bleeding 해결**

- Extrapolation으로 생긴 Seams 문제 해결

**Deferred Shading For MSAA**

- G-Buffer 구성 후 Edge Masking과 Edge를 MSAA로 쉐이딩

**SSAO**

- 간접광의 음영을 위한 SSAO

**Lighting**

- 다양한 텍스쳐(Normal, Metallic, Roughness)와 HDR로 semi unreal PBR 구현

**Water**

- Water의 반사와 Depth 차이를 활용한 투영
- Under Water 상황에서의 필터

**Frustum Culling**

- View Frustum 최적화

**Shaders**

- Fog
  - Depth Buffer를 활용한 안개
- Skybox
  - 큐브맵 없이 쉐이더만을 이용한 실시간 동적 하늘 구현
- Cloud
  - 노이즈를 활용한 구름 형태 및 메쉬 생성
  - 반투명 렌더링
- Grass Leaf Color
  - 불연속적인 잔디 색 경계를 없애기 위한 Color Map 활용
- PostEffect
  - Bloom 및 선형 톤맵핑

---

### **CPU 관점**

**Chunk Management**

- Chunk & Update
  - 32x32x32 청크 구성 및 관리
- Load / Unload
  - 멀티쓰레드로 청크 로딩 관리
- Patch
  - 멀티쓰레드로 청크 패치 관리
  - Tree나 사용자에 의한 인접한 Chunk 수정을 포함
- Picking
  - 3D DDA를 활용한 블록 마우스 피킹

**Mesh Optimization**

- Binary Block Info
  - 메모리를 아끼기 위한 비트단위 데이터 구성
- Binary Greedy Meshing
  - Load 속도를 위한 그리디 비트연산
  - GPU 렌더속도를 위한 그리디 메싱

**World Generation**

- Terrain
  - 지형 결정 방법
- Biome
  - 바이옴 결정 방법
- Tree
  - 바이옴에 따른 트리 결정 방법 및 예외사항
- BlockType
  - 여러 매개변수를 활용한 Block 타입 결정 방법
- Worldmap
  - 전체를 한눈에 볼 수 있는 2D 월드맵

<br />

## 보완 필요 부분

- MirrorPlane Reflection → SSR
- SSAO 쉐이더 비용 절감 필요
- 코드 (static header singleton) 일관성 필요
- GPU Resource 소멸 관리
