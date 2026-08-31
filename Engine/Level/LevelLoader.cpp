#include "LevelLoader.h"
#include <fstream>
#include <cassert>
#include <nlohmann/json.hpp>

// ベースディレクトリ（resourcesフォルダ以下に置く想定）
const std::string LevelLoader::kDefaultBaseDirectory = "resources/levels/";
const std::string LevelLoader::kExtension = ".json";

LevelLoader::LevelData LevelLoader::Load(const std::string& fileName)
{
	// ===== 1. JSONファイルを開く =====

	// 連結してフルパスを得る
	const std::string fullpath = kDefaultBaseDirectory + fileName + kExtension;

	// ファイルストリーム
	std::ifstream file;

	// ファイルを開く
	file.open(fullpath);

	// ファイルオープン失敗をチェック
	if (file.fail()) {
		assert(0 && "LevelLoader: JSONファイルが開けません");
	}

	// ===== 2. ファイルチェック =====

	// JSON文字列から解析したデータ
	nlohmann::json deserialized;

	// 解析
	file >> deserialized;

	// 正しいレベルデータファイルかチェック
	assert(deserialized.is_object());
	assert(deserialized.contains("name"));
	assert(deserialized["name"].is_string());

	// "name" を文字列として取得
	std::string name = deserialized["name"].get<std::string>();

	// 正しいレベルデータファイルかチェック
	assert(name.compare("scene") == 0);

	// ===== 3. レベルデータ格納用インスタンスを生成 =====
	// LevelData型の変数は整理して格納するための入れ物として用意した
	LevelData levelData;

	// ===== 4. オブジェクトリストを再帰的に走査 =====
	// ツリー構造の走査は再帰呼び出しが基本。
	// reserveしないとvectorの引っ越しが発生する危険性あり。
	TraverseObjects(deserialized["objects"], levelData);

	return levelData;
}

void LevelLoader::TraverseObjects(nlohmann::json& objects, LevelData& levelData)
{
	for (nlohmann::json& object : objects) {

		// 種別を取得
		assert(object.contains("type"));
		std::string type = object["type"].get<std::string>();

		// ===== 無効フラグのチェック =====
		// "disabled" が true のオブジェクトはゲームに出さないのでスキップ
		// 子ノードもまとめてスキップされる（再帰呼び出しをしない）
		if (object.contains("disabled") && object["disabled"].get<bool>() == true) {
			continue;
		}

		// ===== MESH =====
		// type が MESH である場合の処理。
		// 「読み込んだレベルデータ」のオブジェクトリストに追加する
		// （本当はMESHを含めた全オブジェクトを追加すべき）
		if (type.compare("MESH") == 0) {

			// 要素追加
			levelData.objects.emplace_back(ObjectData{});

			// 今追加した要素の参照を得る
			ObjectData& objectData = levelData.objects.back();

			// ファイル名
			if (object.contains("file_name")) {
				objectData.fileName = object["file_name"];

				// ===== file_name の自動補完 =====
				// 拡張子がなく、スラッシュも含まない場合（例: "box"）
				// → "box/box.obj" として解決する
				const std::string& fn = objectData.fileName;
				bool hasExtension = fn.find('.') != std::string::npos;
				bool hasSlash     = fn.find('/') != std::string::npos || fn.find('\\') != std::string::npos;
				if (!hasExtension && !hasSlash) {
					objectData.fileName = fn + "/" + fn + ".obj";
				}
			}

			// オブジェクト名
			if (object.contains("name")) {
				objectData.name = object["name"];
			}

			// ===== トランスフォームのパラメータ読み込み =====
			// トランスフォームの各パラメータ「平行移動」「回転角」「スケーリング」を読み込む。
			// この時、Blenderと自分のゲームの座標系の軸方向の違いを吸収しておく。
			// 軸方向変換は、ここでやる方法とエクスポーターでやる方法の二通りある。
			// （エクスポーターでやることが多い）
			nlohmann::json& transform = object["transform"];

			// 平行移動
			objectData.translation.x =  (float)transform["translation"][0];
			objectData.translation.y =  (float)transform["translation"][2];
			objectData.translation.z =  (float)transform["translation"][1];

			// 回転（度数法）
			objectData.rotation.x = -(float)transform["rotation"][0];
			objectData.rotation.y = -(float)transform["rotation"][2];
			objectData.rotation.z = -(float)transform["rotation"][1];

			// スケーリング
			objectData.scaling.x = (float)transform["scaling"][0];
			objectData.scaling.y = (float)transform["scaling"][2];
			objectData.scaling.z = (float)transform["scaling"][1];

			// ===== コライダーのパラメータ読み込み =====
			// Blenderアドオンが出力した "collider" キーがあれば読み込む
			if (object.contains("collider")) {
				nlohmann::json& collider = object["collider"];

				objectData.hasCollider = true;

				// コライダーの種類（"BOX" など）
				if (collider.contains("type")) {
					objectData.colliderType = collider["type"].get<std::string>();
				}

				// コライダーの中心（座標軸変換: Y↔Z 入れ替え）
				if (collider.contains("center")) {
					objectData.colliderCenter.x =  (float)collider["center"][0];
					objectData.colliderCenter.y =  (float)collider["center"][2];
					objectData.colliderCenter.z =  (float)collider["center"][1];
				}

				// コライダーのサイズ（軸変換は不要だがY↔Z対応）
				if (collider.contains("size")) {
					objectData.colliderSize.x = (float)collider["size"][0];
					objectData.colliderSize.y = (float)collider["size"][2];
					objectData.colliderSize.z = (float)collider["size"][1];
				}
			}
		}

		// ===== ツリー構造の再帰処理 =====
		// オブジェクト走査を再帰関数に押し出して再帰呼び出しを行う。
		// TODO: 子ノード走査を再帰呼び出しで走査する
		if (object.contains("children")) {
			TraverseObjects(object["children"], levelData);
		}
	}
}
