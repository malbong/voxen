## 개요

- 프로젝트를 진행하면서 활용한 기능들을 정리한 문서입니다.

<br />

## 목록

### **GPU**

- Cascade ShadowMap
  - 그림자 구현을 위한 쉐도우맵 구현
- MSAA Bleeding 해결
  - Extrapolation으로 생긴 Seams 문제 해결
- Deferred Shading For MSAA
  - G-Buffer 구성 후 Edge Masking으로 Edge에 대한 연산을 따로 MSAA로 쉐이딩
- SSAO
  - 블록의 간접 음영처리를 위한 SSAO
- Lighting
  - 다양한 텍스쳐(Normal, Metalic, Roughness)와 HDR로 semi unreal PBR 구현
- Water
  - Water의 반사와 Depth 차이를 활용한 투영
  - Under Water의 필터
- Frustum Culling
  - View Frustum 최적화
- Shaders
  - Fog
    - Depth를 활용한 안개
  - Skybox
    - 큐브맵을 사용하지 않고 쉐이더만을 이용하여 하늘색 구현
  - Cloud
    - 노이즈를 활용한 구름 형태 생성 및 반투명 렌더링
  - Grass Leaf Color
    - 불연속적인 잔디 색 경계를 없애기 위한 Color Map 활용
  - PostEffect
    - Bloom 및 선형톤맵핑

### **CPU**

- Chunk Management
  - Chunk & Update
    - 32x32x32 청크 구성 및 관리
  - Load / Unload
    - 멀티쓰레드로 청크 로딩 관리
  - Patch
    - 멀티쓰레드로 청크 패치 관리
    - Tree나 사용자에 의한 인접한 Chunk 수정을 포함
  - Picking
    - 3D DDA를 활용한 마우스 피킹
- Mesh Optimization
  - Binary Block Info
    - 메모리를 아끼기 위한 비트단위 블록 데이터 구성
  - Binary Greedy Meshing
    - Chunk Load 속도와 GPU 렌더링 속도를 위한 비트단위 청크 메싱
- World Generation
  - Terrain
    - 지형 결정 방법
  - Biome
    - 바이옴 결정 방법
  - Tree
    - 바이옴에 따른 트리 결정 방법 및 예외사항
  - BlockType
    - 다양한 매개변수를 활용한 Block 타입 결정 방법
  - Worldmap
    - 전체를 한눈에 볼 수 있는 2D 월드맵
