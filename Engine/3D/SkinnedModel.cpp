#include "SkinnedModel.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

Microsoft::WRL::ComPtr<ID3D12RootSignature> SkinnedModel::sRootSignature_;
Microsoft::WRL::ComPtr<ID3D12PipelineState> SkinnedModel::sPipelineState_;

namespace {

	Matrix4x4 MakeSRT(const Vector3& s, const Matrix4x4::Quat& q, const Vector3& t)
	{
		Matrix4x4 m = Matrix4x4::MakeIdentity4x4();

		float xx = q.x * q.x;
		float yy = q.y * q.y;
		float zz = q.z * q.z;
		float xy = q.x * q.y;
		float xz = q.x * q.z;
		float yz = q.y * q.z;
		float wx = q.w * q.x;
		float wy = q.w * q.y;
		float wz = q.w * q.z;

		// 回転部分
		m.m[0][0] = (1.0f - 2.0f * (yy + zz)) * s.x;
		m.m[0][1] = (2.0f * (xy - wz)) * s.x;
		m.m[0][2] = (2.0f * (xz + wy)) * s.x;

		m.m[1][0] = (2.0f * (xy + wz)) * s.y;
		m.m[1][1] = (1.0f - 2.0f * (xx + zz)) * s.y;
		m.m[1][2] = (2.0f * (yz - wx)) * s.y;

		m.m[2][0] = (2.0f * (xz - wy)) * s.z;
		m.m[2][1] = (2.0f * (yz + wx)) * s.z;
		m.m[2][2] = (1.0f - 2.0f * (xx + yy)) * s.z;

		// 平行移動
		m.m[3][0] = t.x;
		m.m[3][1] = t.y;
		m.m[3][2] = t.z;

		return m;
	}

} // namespace


void SkinnedModel::Initialize(DirectXCommon* dx, const std::string& filePath)
{
	dx_ = dx;

	LoadFbx_(filePath);
	CreateBuffers_();
	CreatePipelineIfNeeded_();

	// ==== デバッグ用ボーン回転配列 ====
	debugBoneRot_.resize(bones_.size());
	for (auto& r : debugBoneRot_) {
		r = { 0.0f, 0.0f, 0.0f };
	}
	//デバッグ用ボーン平行移動配列 ====
	debugBoneTrans_.resize(bones_.size());
	for (auto& t : debugBoneTrans_) {
		t = { 0.0f, 0.0f, 0.0f };
	}
}

void SkinnedModel::SetDebugBoneRotate(int index, const Vector3& rot)
{
	if (index < 0) { return; }
	size_t i = static_cast<size_t>(index);
	if (i >= debugBoneRot_.size()) { return; }
	debugBoneRot_[i] = rot;
}

void SkinnedModel::SetDebugBoneTranslate(int index, const Vector3& trans) {
	if (index < 0 || index >= static_cast<int>(debugBoneTrans_.size())) {
		return;
	}
	debugBoneTrans_[index] = trans;
}


void SkinnedModel::LoadFbx_(const std::string& filePath)
{
	Assimp::Importer importer;
	unsigned int flags = 0;
	flags |= aiProcess_Triangulate;
	flags |= aiProcess_JoinIdenticalVertices;
	flags |= aiProcess_GenSmoothNormals;
	flags |= aiProcess_FlipUVs;

	const aiScene* scene = importer.ReadFile(filePath, flags);
	assert(scene && scene->HasMeshes());

	const aiMesh* mesh = scene->mMeshes[0];

	// ======================
	// 1) Node ツリーを構築
	// ======================
	nodes_.clear();
	nodeNameToIndex_.clear();

	std::function<int(const aiNode*, int)> buildNode =
		[&](const aiNode* node, int parentIndex) -> int
		{
			Node nd{};
			nd.name = node->mName.C_Str();
			nd.parentIndex = parentIndex;
			nd.bindLocal = Matrix4x4::FromAiMatrix(node->mTransformation);

			int myIndex = static_cast<int>(nodes_.size());
			nodes_.push_back(nd);
			nodeNameToIndex_[nd.name] = myIndex;

			for (unsigned int i = 0; i < node->mNumChildren; ++i) {
				buildNode(node->mChildren[i], myIndex);
			}
			return myIndex;
		};

	int rootIndex = buildNode(scene->mRootNode, -1);

	// ルートのグローバル逆行列
	{
		Matrix4x4 rootGlobal = Matrix4x4::FromAiMatrix(scene->mRootNode->mTransformation);
		globalInverse_ = Matrix4x4::Inverse(rootGlobal);
	}

	// ======================
 // 2) 頂点・ボーン読み込み
 // ======================

	// ==== 0. 頂点配列（インデックスと同じ数） ====
	vertices_.clear();
	vertices_.resize(mesh->mNumVertices);

	for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
		SkinVertex v{};

		aiVector3D p = mesh->mVertices[i];
		aiVector3D n = mesh->HasNormals() ? mesh->mNormals[i] : aiVector3D(0, 1, 0);
		aiVector3D uv = mesh->HasTextureCoords(0) ? mesh->mTextureCoords[0][i] : aiVector3D(0, 0, 0);

		v.position = { p.x, p.y, p.z, 1.0f };
		v.normal = { n.x, n.y, n.z };
		v.texCoord = { uv.x, 1.0f - uv.y };

		// ボーン情報は一旦全部 0 初期化
		for (int k = 0; k < 4; ++k) {
			v.boneIndex[k] = 0;
			v.boneWeight[k] = 0.0f;
		}

		vertices_[i] = v;
	}

	// ==== 1. ボーン配列とウェイト ====
	bones_.clear();
	bones_.reserve(mesh->mNumBones);

	for (unsigned int bi = 0; bi < mesh->mNumBones; ++bi) {
		const aiBone* bone = mesh->mBones[bi];

		Bone b{};
		b.name = bone->mName.C_Str();
		b.parentIndex = -1; // いったん未設定
		b.offsetMatrix = Matrix4x4::FromAiMatrix(bone->mOffsetMatrix);

		// ★ 対応する Node index を持たせる
		auto itNode = nodeNameToIndex_.find(b.name);
		if (itNode != nodeNameToIndex_.end()) {
			b.nodeIndex = itNode->second;
		} else {
			b.nodeIndex = -1;
		}

		// bones_ の何番目かは bi と一致するので、そのまま push_back
		bones_.push_back(b);

		// ★ 頂点ウェイトを詰める（ここが今コメントアウトされてた部分）
		for (unsigned int wi = 0; wi < bone->mNumWeights; ++wi) {
			const aiVertexWeight& w = bone->mWeights[wi];
			unsigned int vtxId = w.mVertexId;
			float weight = w.mWeight;
			assert(vtxId < vertices_.size());

			// 空いているスロットに詰める（最大4つ）
			for (int k = 0; k < 4; ++k) {
				if (vertices_[vtxId].boneWeight[k] == 0.0f) {
					vertices_[vtxId].boneIndex[k] = static_cast<uint32_t>(bi);
					vertices_[vtxId].boneWeight[k] = weight;
					break;
				}
			}
		}
	}


	// ==== 2. 各頂点のボーンウェイトを正規化 ====
	for (auto& v : vertices_) {
		float sum = v.boneWeight[0] + v.boneWeight[1] +
			v.boneWeight[2] + v.boneWeight[3];
		if (sum > 0.0f) {
			float inv = 1.0f / sum;
			for (int k = 0; k < 4; ++k) {
				v.boneWeight[k] *= inv;
			}
		} else {
			// 1本もウェイトが無い頂点は、ボーン0を1.0にしておく
			v.boneIndex[0] = 0;
			v.boneWeight[0] = 1.0f;
		}
	}

	// ==== 3. Node名 → aiNode* のマップを作成 ====
	std::unordered_map<std::string, const aiNode*> nodeMap;

	std::function<void(const aiNode*)> registerNode = [&](const aiNode* node) {
		nodeMap[node->mName.C_Str()] = node;
		for (unsigned int i = 0; i < node->mNumChildren; ++i) {
			registerNode(node->mChildren[i]);
		}
		};

	registerNode(scene->mRootNode);

	// ==== 4. Bone の parentIndex をセット ====
	for (size_t bi = 0; bi < bones_.size(); ++bi) {
		const std::string& boneName = bones_[bi].name;

		auto nodeIt = nodeMap.find(boneName);
		if (nodeIt == nodeMap.end()) {
			continue; // 対応するノードが無いボーンは親不明のまま
		}

		const aiNode* node = nodeIt->second;
		const aiNode* parent = node->mParent;

		while (parent) {
			const std::string parentName = parent->mName.C_Str();

			auto it = std::find_if(
				bones_.begin(), bones_.end(),
				[&](const Bone& b) { return b.name == parentName; });

			if (it != bones_.end()) {
				bones_[bi].parentIndex = static_cast<int>(std::distance(bones_.begin(), it));
				break;
			}
			parent = parent->mParent;
		}
	}

	// ==== 4.5 Bone 行列の初期化（ボーン数ぶん Identity） ====
	boneMatrices_.clear();
	boneMatrices_.resize(bones_.size());
	for (size_t i = 0; i < boneMatrices_.size(); ++i) {
		boneMatrices_[i] = Matrix4x4::MakeIdentity4x4();
	}

	// ==== 5. アニメーション（とりあえず 1本目）を取り込む ====
	anime_ = AnimationClip{};  // クリア

	if (scene->HasAnimations() && scene->mAnimations[0]) {
		const aiAnimation* aiAnime = scene->mAnimations[0];

		anime_.name = aiAnime->mName.C_Str();

		double ticksPerSecond = aiAnime->mTicksPerSecond;
		if (ticksPerSecond <= 0.0) {
			// 0 の場合は 25fps 相当など、適当な既定値にすることが多い
			ticksPerSecond = 25.0;
		}
		anime_.ticksPerSecond = static_cast<float>(ticksPerSecond);

		// duration（tick）→ 秒換算
		const double durationTicks = aiAnime->mDuration;
		anime_.duration = static_cast<float>(durationTicks / ticksPerSecond);

		// チャンネルごとにコピー
		anime_.channels.clear();
		anime_.channels.reserve(aiAnime->mNumChannels);
		for (unsigned int ci = 0; ci < aiAnime->mNumChannels; ++ci) {
			const aiNodeAnim* nodeAnime = aiAnime->mChannels[ci];
			if (!nodeAnime) { continue; }

			BoneAnimeChannel ch{};
			ch.boneName = nodeAnime->mNodeName.C_Str();

			// どの Bone か
			ch.boneIndex = -1;
			for (size_t bi = 0; bi < bones_.size(); ++bi) {
				if (bones_[bi].name == ch.boneName) {
					ch.boneIndex = static_cast<int>(bi);
					break;
				}
			}

			// ★ どの Node か
			ch.nodeIndex = -1;
			auto itNode = nodeNameToIndex_.find(ch.boneName);
			if (itNode != nodeNameToIndex_.end()) {
				ch.nodeIndex = itNode->second;
			}
			// 対応するボーンが無ければスキップしてもいいし、boneIndex=-1 のままにしてもよい
			// ここでは一旦そのまま保持しておく

			// 位置キー
			ch.posKeys.reserve(nodeAnime->mNumPositionKeys);
			for (unsigned int k = 0; k < nodeAnime->mNumPositionKeys; ++k) {
				const aiVectorKey& key = nodeAnime->mPositionKeys[k];
				AnimKeyVec3 ak{};
				ak.time = static_cast<float>(key.mTime / ticksPerSecond);
				ak.value = { key.mValue.x, key.mValue.y, key.mValue.z };
				ch.posKeys.push_back(ak);
			}

			// 回転キー（クォータニオン）
			ch.rotKeys.reserve(nodeAnime->mNumRotationKeys);
			for (unsigned int k = 0; k < nodeAnime->mNumRotationKeys; ++k) {
				const aiQuatKey& key = nodeAnime->mRotationKeys[k];
				AnimKeyQuat ak{};
				ak.time = static_cast<float>(key.mTime / ticksPerSecond);
				ak.x = key.mValue.x;
				ak.y = key.mValue.y;
				ak.z = key.mValue.z;
				ak.w = key.mValue.w;
				ch.rotKeys.push_back(ak);
			}

			// スケールキー
			ch.scaleKeys.reserve(nodeAnime->mNumScalingKeys);
			for (unsigned int k = 0; k < nodeAnime->mNumScalingKeys; ++k) {
				const aiVectorKey& key = nodeAnime->mScalingKeys[k];
				AnimKeyVec3 ak{};
				ak.time = static_cast<float>(key.mTime / ticksPerSecond);
				ak.value = { key.mValue.x, key.mValue.y, key.mValue.z };
				ch.scaleKeys.push_back(ak);
			}

			anime_.channels.push_back(std::move(ch));
		}
	} else {
		// アニメ無しのモデルの場合：anime_.duration=0 のまま、channels 空
		anime_ = AnimationClip{};
	}


	// ==== 6. mFaces を使って「描画順の頂点配列」を作る ====
	std::vector<SkinVertex> ordered;
	ordered.reserve(mesh->mNumFaces * 3);

	for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
		const aiFace& face = mesh->mFaces[f];
		assert(face.mNumIndices == 3); // Triangulate 済み前提

		for (unsigned int j = 0; j < 3; ++j) {
			unsigned int idx = face.mIndices[j];
			assert(idx < vertices_.size());
			ordered.push_back(vertices_[idx]);
		}
	}

	vertices_.swap(ordered);

	// ==== 7. テクスチャ読み込み（今は白1x1でOK） ====
	texturePath_ = "resources/white1x1.png";
	TextureManager::GetInstance()->LoadTexture(texturePath_);

	// ==== デバッグ: 読み込んだアニメ情報をログ ====
	{
		std::string out =
			"[SkinnedModel] Anim name=" + anime_.name +
			", duration=" + std::to_string(anime_.duration) +
			" sec, ticksPerSec=" + std::to_string(anime_.ticksPerSecond) +
			", channels=" + std::to_string(anime_.channels.size()) + "\n";

		OutputDebugStringA(out.c_str());

		int mappedChannels = 0;
		for (auto& ch : anime_.channels) {
			if (ch.boneIndex >= 0) {
				++mappedChannels;
			}
		}

		std::string out2 =
			"[SkinnedModel] bones=" + std::to_string(bones_.size()) +
			", mappedChannels=" + std::to_string(mappedChannels) + "\n";

		OutputDebugStringA(out2.c_str());
	}

	globalInverse_ = Matrix4x4::Inverse(
		Matrix4x4::FromAiMatrix(scene->mRootNode->mTransformation)
	);

	// ==== 8. GPUリソース作成 ====
	CreateBuffers_();
}
void SkinnedModel::UpdateAnimation(float deltaTime)
{
	if (!animePlaying_ || anime_.duration <= 0.0f ||
		anime_.channels.empty() || bones_.empty() || nodes_.empty()) {
		return;
	}

	// 時間更新
	animeTime_ += deltaTime * animeSpeed_;
	if (animeTime_ > anime_.duration) {
		animeTime_ = std::fmod(animeTime_, anime_.duration);
	}

	const size_t nodeCount = nodes_.size();
	const size_t boneCount = bones_.size();

	// ==== 1) Node ごとのローカル行列 ====
	std::vector<Matrix4x4> localNode(nodeCount);
	for (size_t i = 0; i < nodeCount; ++i) {
		localNode[i] = nodes_[i].bindLocal; // まずはバインド姿勢
	}

	// アニメーションで上書き
	for (const BoneAnimeChannel& ch : anime_.channels) {
		int ni = ch.nodeIndex;
		if (ni < 0 || ni >= static_cast<int>(nodeCount)) {
			continue;
		}

		Vector3          pos = SamplePosition(ch, animeTime_);
		Matrix4x4::Quat  rot = SampleRotation(ch, animeTime_);
		Vector3          scl = SampleScale(ch, animeTime_);

		// ※ スケールが怪しい場合はいったん固定 1 にしてもOK
		// scl = { 1.0f, 1.0f, 1.0f };

		localNode[ni] = MakeSRT(scl, rot, pos);
	}

	// ==== 2) Node ごとのグローバル行列 ====
	std::vector<Matrix4x4> globalNode(nodeCount);
	for (size_t i = 0; i < nodeCount; ++i) {
		int parent = nodes_[i].parentIndex;
		if (parent < 0) {
			globalNode[i] = localNode[i];
		} else {
			globalNode[i] = Matrix4x4::Multiply(globalNode[parent], localNode[i]);
		}
	}

	// ==== 3) Bone 行列 ====
	if (boneMatrices_.size() != boneCount) {
		boneMatrices_.resize(boneCount);
	}

	for (size_t bi = 0; bi < boneCount; ++bi) {
		const Bone& b = bones_[bi];

		if (b.nodeIndex < 0 || b.nodeIndex >= static_cast<int>(nodeCount)) {
			boneMatrices_[bi] = Matrix4x4::MakeIdentity4x4();
			continue;
		}

		Matrix4x4 global = globalNode[b.nodeIndex];

		// 典型的な式は rootInv * global * offset
		// まずは rootInv を無視して挙動確認した方が分かりやすい
		Matrix4x4 finalMat =
			Matrix4x4::Multiply(global, b.offsetMatrix);

		// もし root にスケールや平行移動が入っていてズレる場合だけ、
		// この行を有効にして rootInverse_ を掛ける：
		//
		// finalMat = Matrix4x4::Multiply(globalInverse_, finalMat);

		boneMatrices_[bi] = finalMat;
	}
}

Vector3 SkinnedModel::SamplePosition(const BoneAnimeChannel& ch, float time)
{
	if (ch.posKeys.empty()) return { 0,0,0 };
	if (ch.posKeys.size() == 1) return ch.posKeys[0].value;

	for (size_t i = 0; i < ch.posKeys.size() - 1; i++) {
		float t0 = ch.posKeys[i].time;
		float t1 = ch.posKeys[i + 1].time;
		if (time >= t0 && time <= t1) {
			float f = (time - t0) / (t1 - t0);
			return ch.posKeys[i].value * (1.0f - f) + ch.posKeys[i + 1].value * f;
		}
	}

	return ch.posKeys.back().value;
}

Matrix4x4::Quat SkinnedModel::SampleRotation(const BoneAnimeChannel& ch, float time)
{
	if (ch.rotKeys.empty()) return { 0,0,0,1 };
	if (ch.rotKeys.size() == 1) return {
		ch.rotKeys[0].x, ch.rotKeys[0].y, ch.rotKeys[0].z, ch.rotKeys[0].w
	};

	for (size_t i = 0; i < ch.rotKeys.size() - 1; i++) {
		float t0 = ch.rotKeys[i].time;
		float t1 = ch.rotKeys[i + 1].time;
		if (time >= t0 && time <= t1) {
			float f = (time - t0) / (t1 - t0);
			Matrix4x4::Quat a{ ch.rotKeys[i].x, ch.rotKeys[i].y, ch.rotKeys[i].z, ch.rotKeys[i].w };
			Matrix4x4::Quat b{ ch.rotKeys[i + 1].x, ch.rotKeys[i + 1].y, ch.rotKeys[i + 1].z, ch.rotKeys[i + 1].w };
			return Matrix4x4::Slerp(a, b, f);
		}
	}

	Matrix4x4::Quat last{ ch.rotKeys.back().x, ch.rotKeys.back().y, ch.rotKeys.back().z, ch.rotKeys.back().w };
	return last;
}

Vector3 SkinnedModel::SampleScale(const BoneAnimeChannel& ch, float time)
{
	// スケールキーが無い → (1,1,1) 固定
	if (ch.scaleKeys.empty()) return { 1.0f, 1.0f, 1.0f };
	if (ch.scaleKeys.size() == 1) return ch.scaleKeys[0].value;

	for (size_t i = 0; i < ch.scaleKeys.size() - 1; i++) {
		float t0 = ch.scaleKeys[i].time;
		float t1 = ch.scaleKeys[i + 1].time;
		if (time >= t0 && time <= t1) {
			float f = (time - t0) / (t1 - t0);
			return ch.scaleKeys[i].value * (1.0f - f) + ch.scaleKeys[i + 1].value * f;
		}
	}

	return ch.scaleKeys.back().value;
}


void SkinnedModel::CreateBuffers_()
{
	// VertexBuffer
	size_t vtxSize = sizeof(SkinVertex) * vertices_.size();
	vertexResource_ = dx_->CreateBufferResource(vtxSize);

	SkinVertex* vtxData = nullptr;
	vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vtxData));
	std::memcpy(vtxData, vertices_.data(), vtxSize);
	vertexResource_->Unmap(0, nullptr);

	vbView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
	vbView_.StrideInBytes = sizeof(SkinVertex);
	vbView_.SizeInBytes = static_cast<UINT>(vtxSize);

	// Material CB
	materialResource_ = dx_->CreateBufferResource(sizeof(MaterialCBData));
	materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData_));
	materialData_->color = { 1.0f,1.0f,1.0f,1.0f };
	materialData_->enableLighting = 0;
	materialData_->uvTransform = Matrix4x4::MakeIdentity4x4();

	// Transform CB
	transformResource_ = dx_->CreateBufferResource(sizeof(TransformCBData));
	transformResource_->Map(0, nullptr, reinterpret_cast<void**>(&transformData_));

	// Bone CB
	if (!boneMatrices_.empty()) {
		boneMatrixResource_ =
			dx_->CreateBufferResource(sizeof(Matrix4x4) * static_cast<UINT>(boneMatrices_.size()));
		boneMatrixResource_->Map(0, nullptr, reinterpret_cast<void**>(&boneMatrixData_));
		std::memcpy(boneMatrixData_, boneMatrices_.data(),
			sizeof(Matrix4x4) * boneMatrices_.size());
	}
}

void SkinnedModel::CreatePipelineIfNeeded_()
{
	if (sRootSignature_ && sPipelineState_) {
		return;
	}

	ID3D12Device* device = dx_->GetDevice();

	// ==== RootSignature ====
	using Microsoft::WRL::ComPtr;

	D3D12_ROOT_PARAMETER rootParams[4]{};

	// b0: Material
	rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParams[0].Descriptor.ShaderRegister = 0;
	rootParams[0].Descriptor.RegisterSpace = 0;
	rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	// b1: Transform
	rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParams[1].Descriptor.ShaderRegister = 1;
	rootParams[1].Descriptor.RegisterSpace = 0;
	rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

	// t0: Texture SRV (descriptor table)
	D3D12_DESCRIPTOR_RANGE srvRange{};
	srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	srvRange.NumDescriptors = 1;
	srvRange.BaseShaderRegister = 0;
	srvRange.RegisterSpace = 0;
	srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParams[2].DescriptorTable.NumDescriptorRanges = 1;
	rootParams[2].DescriptorTable.pDescriptorRanges = &srvRange;
	rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	// b2: BoneMatrices
	rootParams[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParams[3].Descriptor.ShaderRegister = 2;
	rootParams[3].Descriptor.RegisterSpace = 0;
	rootParams[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

	// Sampler
	D3D12_STATIC_SAMPLER_DESC sampler{};
	sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	sampler.MaxLOD = D3D12_FLOAT32_MAX;
	sampler.ShaderRegister = 0;
	sampler.RegisterSpace = 0;
	sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_ROOT_SIGNATURE_DESC rsDesc{};
	rsDesc.Flags =
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	rsDesc.NumParameters = _countof(rootParams);
	rsDesc.pParameters = rootParams;
	rsDesc.NumStaticSamplers = 1;
	rsDesc.pStaticSamplers = &sampler;

	ComPtr<ID3DBlob> rsBlob;
	ComPtr<ID3DBlob> errorBlob;
	HRESULT hr = D3D12SerializeRootSignature(
		&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		&rsBlob, &errorBlob);
	assert(SUCCEEDED(hr));

	hr = device->CreateRootSignature(
		0, rsBlob->GetBufferPointer(),
		rsBlob->GetBufferSize(),
		IID_PPV_ARGS(&sRootSignature_));
	assert(SUCCEEDED(hr));

	// ==== PSO ====
	ComPtr<IDxcBlob> vsBlob = dx_->CompilesSharder(
		L"resources/shaders/Skinned.VS.hlsl", L"vs_6_0");
	ComPtr<IDxcBlob> psBlob = dx_->CompilesSharder(
		L"resources/shaders/Skinned.PS.hlsl", L"ps_6_0");

	D3D12_INPUT_ELEMENT_DESC inputElements[] = {
		{ "POSITION",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL",      0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD",    0, DXGI_FORMAT_R32G32_FLOAT,       0, 28, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "BLENDINDICES",0, DXGI_FORMAT_R32G32B32A32_UINT,  0, 36, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 52, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
	psoDesc.pRootSignature = sRootSignature_.Get();
	psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
	psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
	psoDesc.BlendState = dx_->GetBlendDesc();       // 既存のヘルパーがある前提
	psoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
	psoDesc.RasterizerState = dx_->GetRasterizerDesc(); // 既存
	psoDesc.DepthStencilState = dx_->GetDepthStencilDesc(); // 既存
	psoDesc.InputLayout = { inputElements, _countof(inputElements) };
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = dx_->GetRTVFormat();
	psoDesc.DSVFormat = dx_->GetDSVFormat();
	psoDesc.SampleDesc.Count = 1;
	psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

	hr = device->CreateGraphicsPipelineState(
		&psoDesc, IID_PPV_ARGS(&sPipelineState_));
	assert(SUCCEEDED(hr));
}

void SkinnedModel::Draw()
{
	auto* cmd = dx_->GetCommandList();

	// === Transform 計算 ===
	Matrix4x4 world =
		Matrix4x4::MakeAffineMatrix(scale_, rotate_, translate_);

	Matrix4x4 camMat =
		Matrix4x4::MakeAffineMatrix(
			cameraScale_, cameraRotate_, cameraTranslate_);
	Matrix4x4 view = Matrix4x4::Inverse(camMat);

	float aspect =
		static_cast<float>(WinApp::kClientWidth) /
		static_cast<float>(WinApp::kClientHeight);
	Matrix4x4 proj =
		Matrix4x4::PerspectiveFov(0.45f, aspect, 0.1f, 1000.0f);

	Matrix4x4 vp = Matrix4x4::Multiply(view, proj);
	Matrix4x4 wvp = Matrix4x4::Multiply(world, vp);

	transformData_->world = world;
	transformData_->worldViewProj = wvp;

	//// ==== Bone 行列のデバッグ更新 ====
	//if (debugPoseEnable_ && !bones_.empty()) {
	//    const size_t boneCount = bones_.size();

	//    // ローカル行列（回転＋平行移動）
	//    std::vector<Matrix4x4> local(boneCount);
	//    for (size_t i = 0; i < boneCount; ++i) {
	//        const Vector3& r = debugBoneRot_[i];
	//        const Vector3& t = debugBoneTrans_[i];  // ★ 追加

	//        local[i] = Matrix4x4::MakeAffineMatrix(
	//            { 1.0f, 1.0f, 1.0f },  // scale
	//            r,                     // rotate（ラジアン）
	//            t                      // translate（ImGui から設定）
	//        );
	//    }


	//    // 親子を考慮したグローバル（簡易版）
	//    std::vector<Matrix4x4> global(boneCount);
	//    for (size_t i = 0; i < boneCount; ++i) {
	//        int parent = bones_[i].parentIndex;
	//        if (parent < 0) {
	//            global[i] = local[i];
	//        } else {
	//            global[i] = Matrix4x4::Multiply(global[parent], local[i]);
	//        }
	//        // 今は offset を無視して「デバッグ用行列」としてそのまま使う
	//        boneMatrices_[i] = global[i];
	//    }
	//}

	// Bone 行列を CB にコピー
	if (boneMatrixData_ && !boneMatrices_.empty()) {
		std::memcpy(boneMatrixData_,
			boneMatrices_.data(),
			sizeof(Matrix4x4) * boneMatrices_.size());
	}

	// ==== 描画コマンド ====
	// PSO/RootSig
	cmd->SetGraphicsRootSignature(sRootSignature_.Get());
	cmd->SetPipelineState(sPipelineState_.Get());

	// IA
	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmd->IASetVertexBuffers(0, 1, &vbView_);

	// DescriptorHeap（テクスチャ）
	ID3D12DescriptorHeap* heaps[] = {
		TextureManager::GetInstance()->GetSrvDescriptorHeap()
	};
	cmd->SetDescriptorHeaps(1, heaps);

	// RootParameter 0: Material
	cmd->SetGraphicsRootConstantBufferView(
		0, materialResource_->GetGPUVirtualAddress());

	// RootParameter 1: Transform
	cmd->SetGraphicsRootConstantBufferView(
		1, transformResource_->GetGPUVirtualAddress());

	// RootParameter 2: Texture SRV
	D3D12_GPU_DESCRIPTOR_HANDLE texHandle =
		TextureManager::GetInstance()->GetSrvHandleGPU(texturePath_);
	cmd->SetGraphicsRootDescriptorTable(2, texHandle);

	// RootParameter 3: BoneMatrices
	if (boneMatrixResource_) {
		cmd->SetGraphicsRootConstantBufferView(
			3, boneMatrixResource_->GetGPUVirtualAddress());
	}

	cmd->DrawInstanced(
		static_cast<UINT>(vertices_.size()), 1, 0, 0);
}
