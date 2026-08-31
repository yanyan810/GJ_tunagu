#include "Particle.h"
#include "ParticleCommon.h"
#include "imgui.h"
#include "Camera.h"


//Vector3 Normalize(const Vector3& v) {
//	float length = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
//	if (length == 0.0f) return { 0.0f, 0.0f, 0.0f };
//	return { v.x / length, v.y / length, v.z / length };
//}

// === AABB と点の当たり判定 ===
static bool IsCollision(const AABB& aabb, const Vector3& point) {
	if (aabb.min.x <= point.x && point.x <= aabb.max.x &&
		aabb.min.y <= point.y && point.y <= aabb.max.y &&
		aabb.min.z <= point.z && point.z <= aabb.max.z) {
		return true;
	}
	return false;
}

void Particle::Initialize(ParticleCommon* particleCommon, DirectXCommon* dx, SrvManager* srv) {
	this->particleCommon = particleCommon;
	dx_ = dx;

	srv_ = srv;


	// ライト周りは今のままでOK
	directionalLightResource = dx->CreateBufferResource(sizeof(DirectionalLight));
	directionalLightResource->Map(0, nullptr,
		reinterpret_cast<void**>(&directionalLightData));
	directionalLightData->color = { 1.0f,1.0f,1.0f,1.0f };
	directionalLightData->direction = Matrix4x4::Normalize(Vector3({ 0.0f,-1.0f,0.0f }));
	directionalLightData->intensity = 1.0f;

	// カメラ
	cameraTransform = {
	{1,1,1},
	{0, std::numbers::pi_v<float>, 0}, // 真後ろ向き
	{0,4, 20}        // 位置を反対側にする
	};

	// ===== instancing 用 StructuredBuffer =====
	instancingResource_ =
		dx->CreateBufferResource(sizeof(ParticleForGPU) * kMaxInstance);
	instancingResource_->SetName(L"ParticleInstancingBuffer");
	instancingResource_->Map(0, nullptr,
		reinterpret_cast<void**>(&instancingData_));

	instancingSrvIndex_ = srv_->Allocate();
	D3D12_CPU_DESCRIPTOR_HANDLE cpu = srv_->GetCPUDescriptionHandle(instancingSrvIndex_);
	instancingSrvHandleGPU_ = srv_->GetGPUDescriptionHandle(instancingSrvIndex_);

	D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
	desc.Format = DXGI_FORMAT_UNKNOWN;
	desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	desc.Buffer.FirstElement = 0;
	desc.Buffer.NumElements = kMaxInstance;
	desc.Buffer.StructureByteStride = sizeof(ParticleForGPU);
	desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

	dx_->GetDevice()->CreateShaderResourceView(instancingResource_.Get(), &desc, cpu);


	// ランダム分布
	std::uniform_real_distribution<float> distPos(-10.0f, 10.0f);   // 位置のランダム範囲
	std::uniform_real_distribution<float> distVel(-0.2f, 0.2f);     // 速度のランダム範囲
	std::uniform_real_distribution<float> distColor(0.0f, 1.0f);

	emitter.count = 3;
	emitter.frequency = 0.5f;
	emitter.frequencyTime = 0.0f;

	emitter.transform.translate = { 0.0f,0.0f,0.0f };
	emitter.transform.scale = { 1.0f,1.0f,1.0f };
	emitter.transform.rotate = { 0.0f,0.0f,0.0f };

	accelerationField.acceleration={ 15.0f,0.0f,0.0f };
	accelerationField.area.min = { -1.0f, -1.0f, -1.0f };
	accelerationField.area.max = { 1.0f,  1.0f,  1.0f };
}

Particle::ParticleData Particle::MakeNewParticle(std::mt19937& ramdomEngine, const Vector3& translate) {

	std::uniform_real_distribution<float> distribution(-1.0f, 1.0f);
	std::uniform_real_distribution<float> distColor(0.0f, 1.0f);
	std::uniform_real_distribution<float> distTime(1.0f, 3.0f);
	Vector3 randomTranslate{ distribution(randomEngine),distribution(randomEngine),distribution(randomEngine) };

	Particle::ParticleData particle;
	particle.transform.scale = { 1.0f, 1.0f, 1.0f };
	particle.transform.rotate = { 0.0f, 1.55f, 0.0f };
	particle.transform.translate = translate + randomTranslate;
	particle.color = { distColor(ramdomEngine),distColor(ramdomEngine),distColor(ramdomEngine) ,1.0f };
	particle.velocity = { distribution(ramdomEngine),distribution(ramdomEngine),distribution(ramdomEngine) };
	particle.lifeTime = distTime(randomEngine);
	particle.currentTime = 0;

	return particle;

}


Particle::ParticleData Particle::MakeNewParticleHit(std::mt19937& randomEngine, const Vector3& translate) {
	ParticleData p{};
	p.type = ParticleType::Hit;
	p.transform.translate = translate;
	p.color = { 1,1,1,1 };
	p.velocity = { 0,0,0 };
	p.lifeTime = hitLifeTime_;  
	p.currentTime = 0.0f;

	p.transform.scale = { hitBaseScaleX_, 1.0f, 1.0f };
	p.transform.rotate = { 0.0f, 0.0f, 0.0f };
	return p;
}

void Particle::UpdateNormal(ParticleData& p) {
	if (IsCollision(accelerationField.area, p.transform.translate)) {
		p.velocity += accelerationField.acceleration * deltaTime;
	}

	p.transform.translate += p.velocity * deltaTime;

	BuildWorld_Normal(p);
}

void Particle::UpdateHit(ParticleData& p) {
	// 移動しない（or 少しだけ）
	// p.transform.translate += p.velocity * deltaTime;

	BuildWorld_Hit(p);
}



void Particle::Update() {
	instanceCount_ = 0;

	// ★これを呼ぶ
	MakeCameraMatrices();

	for (auto it = particles.begin(); it != particles.end() && instanceCount_ < kMaxInstance; ) {
		ParticleData& p = *it;

		if (p.lifeTime <= p.currentTime) {
			it = particles.erase(it);
			continue;
		}

		p.currentTime += deltaTime;

		// Normal/Hit 分岐（type 必須）
		if (p.type == ParticleType::Normal) {
			UpdateNormal(p); // この中で BuildWorld_Normal(p) を呼ぶ
		}
		else {
			UpdateHit(p);    // この中で BuildWorld_Hit(p) を呼ぶ
		}

		++it;
	}
}


void Particle::SpawnParticle() {

	emitter.frequencyTime += deltaTime;
	if (emitter.frequency <= emitter.frequencyTime) {
		particles.splice(particles.end(), Emit(emitter, randomEngine));
		emitter.frequencyTime -= emitter.frequency;

	}


}

void Particle::Draw() {
	assert(instancingSrvHandleGPU_.ptr != 0);

	// ※ SRV heap は main 側で srvManager->PreDraw() により一元管理する
// ※ Draw 内では SetDescriptorHeaps を呼ばないこと

	auto* cmd = dx_->GetCommandList();

	// ★ ここで SetDescriptorHeaps しない（heapは main の srvManager->PreDraw() で統一）
	cmd->SetGraphicsRootDescriptorTable(1, instancingSrvHandleGPU_);
	cmd->SetGraphicsRootConstantBufferView(3, directionalLightResource->GetGPUVirtualAddress());

	if (model_) model_->Draw(cmd, instanceCount_);
}



void Particle::SetModel(const std::string& filePath) {
	//モデルを検索してセットする
	auto* mgr = ModelManager::GetInstance();

	// まず探す
	Model* m = mgr->FindModel(filePath);

	// なければロードして再取得
	if (!m) {
		mgr->LoadModel(filePath);
		m = mgr->FindModel(filePath);
	}

	model_ = m;

}

void Particle::DebugImGui() {
	ImGui::Begin("Particle Camera");

	// 位置
	ImGui::DragFloat3(
		"Camera Pos",
		&cameraTransform.translate.x,
		0.1f
	);

	// 回転（ラジアンそのまま）
	ImGui::DragFloat3(
		"Camera Rot (rad)",
		&cameraTransform.rotate.x,
		0.01f
	);

	// 距離だけ別スライダーでいじりたいなら
	// Zだけ出して、変更を反映する方法もあり
	float dist = cameraTransform.translate.z;
	if (ImGui::SliderFloat("Dist(Z)", &dist, -100.0f, 100.0f)) {
		cameraTransform.translate.z = dist;
	}

	ImGui::DragFloat3("EmitterTranslate", &emitter.transform.translate.x, 0.01f, -100.0f, 100.0f);

	if (ImGui::Button("AddParticle")) {
		particles.splice(particles.end(), Emit(emitter, randomEngine));
	}

	ImGui::Separator();
	ImGui::Text("Acceleration Field");

	ImGui::DragFloat3("Field Min", &accelerationField.area.min.x, 0.1f);
	ImGui::DragFloat3("Field Max", &accelerationField.area.max.x, 0.1f);
	ImGui::DragFloat3("Field Accel", &accelerationField.acceleration.x, 0.1f);

	ImGui::Separator();
	ImGui::Text("HitEffect");

	ImGui::DragInt("Hit Count", reinterpret_cast<int*>(&hitCount_), 1, 1, 64);
	ImGui::DragFloat("Hit Life", &hitLifeTime_, 0.01f, 0.05f, 5.0f);
	ImGui::DragFloat("BaseScaleX", &hitBaseScaleX_, 0.01f, 0.01f);

	ImGui::DragFloat("Scale Min", &hitScaleMin_, 0.01f, 0.0f, 10.0f);
	ImGui::DragFloat("Scale Max", &hitScaleMax_, 0.01f, 0.0f, 10.0f);
	if (hitScaleMin_ > hitScaleMax_) std::swap(hitScaleMin_, hitScaleMax_);

	if (ImGui::Button("Spawn Hit @ Emitter")) {
		SpawnHit(emitter.transform.translate);
	}


	ImGui::End();
}

std::list<Particle::ParticleData>
Particle::Emit(const Particle::Emitter& emitter, std::mt19937& randomEngine) {
	std::list<ParticleData> result;

	for (uint32_t count = 0; count < emitter.count; ++count) {
		ParticleData p = MakeNewParticle(randomEngine, emitter.transform.translate);
		p.type = ParticleType::Normal;   // ★必須
		result.push_back(p);
	}

	return result;
}


void Particle::SpawnHit(const Vector3& pos) {
	const float pi = std::numbers::pi_v<float>;
	const float step = (2.0f * pi) / float(hitCount_);

	std::uniform_real_distribution<float> distLen(hitScaleMin_, hitScaleMax_);
	std::uniform_real_distribution<float> distJitter(
		-hitJitterDeg_ * (pi / 180.0f),
		hitJitterDeg_ * (pi / 180.0f)
	);

	for (uint32_t i = 0; i < hitCount_; ++i) {
		ParticleData p = MakeNewParticleHit(randomEngine, pos);

		const float angle = step * float(i) + distJitter(randomEngine);
		const float len = distLen(randomEngine);

		p.transform.rotate = { 0.0f, 0.0f, angle };
		p.transform.scale = { hitBaseScaleX_, len, 1.0f };

		p.transform.translate = {
			pos.x + std::cosf(angle) * hitRadius_,
			pos.y + std::sinf(angle) * hitRadius_,
			pos.z
		};

		particles.push_back(p);
	}
}

void Particle::BuildWorld_Hit(const ParticleData& p) {
	// billboardMatrix_ は「回転だけ」のカメラ行列（MakeCameraMatricesで作成済み）

	// カメラ基底（あなたの行列は “列に基底が入る” 前提の書き方をしているので列から取る）
	Vector3 camRight = { billboardMatrix_.m[0][0], billboardMatrix_.m[1][0], billboardMatrix_.m[2][0] };
	Vector3 camUp = { billboardMatrix_.m[0][1], billboardMatrix_.m[1][1], billboardMatrix_.m[2][1] };
	Vector3 camFwd = { billboardMatrix_.m[0][2], billboardMatrix_.m[1][2], billboardMatrix_.m[2][2] };

	// ★星形の「面内回転」：Right/Up を 2D回転する（回転軸は camFwd と同等）
	const float a = p.transform.rotate.z;
	const float c = std::cosf(a);
	const float s = std::sinf(a);

	Vector3 rightR = camRight * c + camUp * (-s);
	Vector3 upR = camRight * s + camUp * (c);

	// あなたの plane.obj は XZ 平面なので、長さ方向を “Up(縦)” にしたいならこれでOK
	// （fixM を行列掛けで入れる代わりに、軸を直接使うので fixM は不要になります）

	Matrix4x4 world = Matrix4x4::MakeIdentity4x4();

	// 列0 = Right, 列1 = Up, 列2 = Forward（スケールを掛ける）
	world.m[0][0] = rightR.x * p.transform.scale.x;
	world.m[1][0] = rightR.y * p.transform.scale.x;
	world.m[2][0] = rightR.z * p.transform.scale.x;

	world.m[0][1] = upR.x * p.transform.scale.y;
	world.m[1][1] = upR.y * p.transform.scale.y;
	world.m[2][1] = upR.z * p.transform.scale.y;

	world.m[0][2] = camFwd.x * p.transform.scale.z;
	world.m[1][2] = camFwd.y * p.transform.scale.z;
	world.m[2][2] = camFwd.z * p.transform.scale.z;

	// 平行移動（あなたは m[3][0..2] を平行移動に使ってる書き方）
	world.m[3][0] = p.transform.translate.x;
	world.m[3][1] = p.transform.translate.y;
	world.m[3][2] = p.transform.translate.z;
	world.m[3][3] = 1.0f;

	Matrix4x4 wvp = Matrix4x4::Multiply(world, vp_);

	instancingData_[instanceCount_].World = world;
	instancingData_[instanceCount_].WVP = wvp;
	instancingData_[instanceCount_].color = p.color;
	instancingData_[instanceCount_].color.w = 1.0f - (p.currentTime / p.lifeTime);

	++instanceCount_;
}




void Particle::MakeCameraMatrices() {
	Matrix4x4 cameraMatrix{};
	Matrix4x4 vp{};

	if (camera_) {
		cameraMatrix = camera_->GetWorldMatrix();
		vp = camera_->GetViewProjectionMatrix();
	}
	else {
		cameraMatrix = Matrix4x4::MakeAffineMatrix(
			cameraTransform.scale,
			cameraTransform.rotate,
			cameraTransform.translate);

		Matrix4x4 viewMatrix = Matrix4x4::Inverse(cameraMatrix);

		Matrix4x4 projMatrix =
			Matrix4x4::PerspectiveFov(
				0.45f,
				float(WinApp::kClientWidth) / float(WinApp::kClientHeight),
				0.1f, 100.0f);

		vp = Matrix4x4::Multiply(viewMatrix, projMatrix);
	}

	// billboard（移動成分を消す）
	billboardMatrix_ = cameraMatrix;
	billboardMatrix_.m[3][0] = 0.0f;
	billboardMatrix_.m[3][1] = 0.0f;
	billboardMatrix_.m[3][2] = 0.0f;

	vp_ = vp;
}

void Particle::BuildWorld_Normal(ParticleData& p) {
	Matrix4x4 fix = Matrix4x4::RotateX(-std::numbers::pi_v<float> *0.5f);
	Matrix4x4 scaleM = Matrix4x4::MakeScaleMatrix(p.transform.scale);
	Matrix4x4 translateM = Matrix4x4::Translation(p.transform.translate);
	Matrix4x4 rotM = Matrix4x4::MakeIdentity4x4();

	Matrix4x4 world =
		Matrix4x4::Multiply(
			Matrix4x4::Multiply(
				Matrix4x4::Multiply(
					Matrix4x4::Multiply(fix, rotM),
					billboardMatrix_),
				scaleM),
			translateM);

	Matrix4x4 wvp = Matrix4x4::Multiply(world, vp_);

	instancingData_[instanceCount_].World = world;
	instancingData_[instanceCount_].WVP = wvp;
	instancingData_[instanceCount_].color = p.color;

	++instanceCount_;
}
