# 온프리지 펌웨어 개발

## 개발 기간

- 2024.12.23~

## 개발 환경 세팅
Docker를 사용하여 개발 환경을 만들었습니다.

## 브랜치 운영 계획(Gitflow)

- **main**: 안정적인 버전
- **release**: 개발 완성된 버전(버그를 여기서 해결)
- **develop**: feature브랜치 통합
- **feature**: 기능 개발

### 브랜치 운영 절차

1. **feature 브랜치에서 개발**: 각 기능 개발은 `feature/xxx`라는 이름의 브랜치에서 작업합니다.
2. 개발이 완료되면 `develop` 브랜치에 해당 `feature` 브랜치를 병합합니다.
3. `main`과 `release` 브랜치는 오상영이 관리합니다.
4. `release`에서 버그 해결을 완료한 후, `main`, `develop`, `feature` 브랜치에 모두 동기화합니다.

## Docker 사용 방법

### 1. Docker Desktop 설치 및 실행

- Docker Desktop을 설치하고 실행합니다.

### 2. Docker 이미지 빌드(vscode 터미널에서 하면 됨)

- 전체 빌드(처음에만):

```bash
docker-compose up build
```

- 수정한 내용만 빌드(개발할 때):

```bash
docker-compose up -d build
```

## 개발할 때

### 1. 로컬 브랜치 생성 및 전환

```
git checkout -b feature/xxx
```

- `feature/xxx`라는 이름의 로컬 브랜치를 생성하고 해당 브랜치로 이동합니다.
- 로컬에 이미 feature/xxx가 있으면 -b는 빼도 됩니다.

### 2. 원격 저장소와 동기화

```
git pull origin feature/xxx
```

- 원격 저장소의 `feature/xxx`와 로컬의 `feature/xxx`를 동기화합니다.

### 3. 개발 후 커밋 및 푸시

- 개발을 완료한 후, 변경 사항을 커밋하고 푸시합니다.

```
git add .
git commit -m "커밋 제목"
git push origin feature/xxx
```

### 4. 브랜치 병합 (feature → develop)

1. `develop` 브랜치로 이동:

```
git checkout develop
```

2. `feature/xxx` 브랜치를 `develop` 브랜치에 병합:

```
git merge feature/xxx
```
