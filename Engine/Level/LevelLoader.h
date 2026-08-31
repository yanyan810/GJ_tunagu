#pragma once
#include <string>
#include <vector>
#include "MathStruct.h"
#include <nlohmann/json.hpp>

/// <summary>
/// Blenderアドオンが出力したJSONシーンファイルを読み込むクラス
/// </summary>
class LevelLoader
{
public:

	// オブジェクト1個分のデータ
	struct ObjectData {
		std::string name;           // オブジェクト名
		std::string fileName;       // カスタムプロパティ "file_name"

		// 無効フラグ（trueならゲームに出さない）
		bool disabled = false;

		// トランスフォーム
		Vector3 translation = { 0.0f, 0.0f, 0.0f };
		Vector3 rotation    = { 0.0f, 0.0f, 0.0f }; // 度数法 (Degree)
		Vector3 scaling     = { 1.0f, 1.0f, 1.0f };

		// コライダー（オプション）
		bool hasCollider = false;
		std::string colliderType;                       // "BOX" など
		Vector3 colliderCenter = { 0.0f, 0.0f, 0.0f };
		Vector3 colliderSize   = { 1.0f, 1.0f, 1.0f };
	};

	// シーン全体のデータ
	struct LevelData {
		std::vector<ObjectData> objects; // MESHオブジェクトのリスト
	};

	/// <summary>
	/// Blender出力JSONファイルを読み込む
	/// </summary>
	/// <param name="fileName">ファイル名（例: "scene.json"）</param>
	/// <returns>読み込んだレベルデータ</returns>
	static LevelData Load(const std::string& fileName);

private:
	// ファイルのベースディレクトリ
	static const std::string kDefaultBaseDirectory;
	// ファイルの拡張子
	static const std::string kExtension;

	/// <summary>
	/// オブジェクトリストを再帰的に走査してLevelDataに追加する
	/// （ツリー構造の走査）
	/// </summary>
	/// <param name="objects">走査対象のJSONオブジェクト配列</param>
	/// <param name="levelData">格納先のLevelData</param>
	static void TraverseObjects(nlohmann::json& objects, LevelData& levelData);
};
