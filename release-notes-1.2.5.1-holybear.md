# NitLink 1.2.5.1 HolyBear Edition

**非官方社群整合版（Unofficial Community Edition）。**

以 NitLink 1.2.5 為基礎，保留 HolyBear Edition 的擷取、畫面、音訊、遊戲收藏及 Discord 功能；本版本不是 nitlink-dev 官方發行版。

## 本次更新

### 語言與遊戲名稱

- 設定介面新增日文與韓文，現在支援 English、繁體中文、簡體中文、德文、日文及韓文。
- 遊戲名稱與封面搜尋會依目前介面語言選擇合適名稱；找不到該語言的資料時才回退至英文。
- 關於頁的版本基礎說明更新為 NitLink 1.2.5。

### 畫面更新與擷取裝置

- 改善來源畫面更新率判斷，減少顯示落後或錯亂的情況。
- 修正調整視窗大小或 HDR 顯示格式切換後，畫面可能無法恢復的問題。
- 改善擷取裝置自動選擇，優先辨識擷取卡，避免一般攝影機搶先被選取；手動選擇與已儲存的裝置仍會優先保留。
- GC553Pro／Switch 2 相關新修正已完成真實實機驗收。

### 保留的 HolyBear Edition 功能

- 保留 GC553Pro 的 HDMI 來源、訊號同步、HDR、VRR、No Signal 畫面及手動來源名稱。
- 保留 FPS 顯示與畫面更新限制；自動模式無法取得更新率時仍使用 30 Hz fallback。
- 保留低延遲音訊、裝置切換回復及音訊診斷。
- 保留遊戲收藏、封面選擇、Discord Rich Presence、視窗標題、設定保存與重置功能。

## 更新方式

1.2.4 及更早版本使用舊的 GitHub 更新檢查網址。Repository 改名後，這些版本可能無法自動收到更新通知；請直接前往[最新版本頁面](https://github.com/HolyBearTW/nitlink-holybear-edition/releases)下載並安裝新版。使用 Setup 升級可保留既有使用者設定。

## 測試與驗證

- `Verification.zip` 包含 SHA256 校驗清單及 GPG 簽章，供需要驗證下載檔案完整性與來源的使用者使用，一般安裝不需要下載。
- 完整建置與 CTest 結果會由本次實際封裝流程附加；本文不預填未執行的測試數字。
- 本機圖片不會上傳至 Discord；Discord 封面顯示仍取決於 Discord 桌面程式與其圖片代理實際讀取結果。

## 已知限制

- 本次沒有新增 PlayStation 官方封面搜尋；PS5 封面來源範圍不變。
- Nintendo Taiwan 圖片只會在有對應素材時出現，且不代表每張圖都含繁體中文標題。
- Discord 活動成功送出不代表圖片已由每個 Discord 客戶端下載並顯示。
- Content FPS 表示畫面內容變化速度，不是遊戲內部 FPS。本版本沒有提供 Steam Frame Generation。

## 來源、授權與致謝

- 原始專案：@nitlink-dev，[nitlink-dev/nitlink](https://github.com/nitlink-dev/nitlink)。本次上游執行期修正來自 [NitLink v1.2.5](https://github.com/nitlink-dev/nitlink/releases/tag/v1.2.5)。
- 保留 [#23](https://github.com/nitlink-dev/nitlink/pull/23)、[#25](https://github.com/nitlink-dev/nitlink/pull/25) 與 [#31](https://github.com/nitlink-dev/nitlink/pull/31) 的 HDMI／VRR、手動來源與 FPS 顯示修改。
- 韓文翻譯由 @i0rph 提供，見 [上游 PR #37](https://github.com/nitlink-dev/nitlink/pull/37)；德文翻譯由 @Noka123 提供；日文介面翻譯由 HolyBear Edition 製作。
- 低延遲音訊與簡體中文：CN_115（@CN115）。整合與維護：聖小熊（@HolyBearTW），並保留 Nathan K.（@n810K）等既有貢獻者資訊。
- [MIT 授權](https://github.com/HolyBearTW/nitlink-holybear-edition/blob/holybear/integrated/LICENSE)、[第三方授權](https://github.com/HolyBearTW/nitlink-holybear-edition/blob/holybear/integrated/LICENSES.md)、[致謝](https://github.com/HolyBearTW/nitlink-holybear-edition/blob/holybear/integrated/ACKNOWLEDGMENTS.md)與[來源紀錄](https://github.com/HolyBearTW/nitlink-holybear-edition/blob/holybear/integrated/HOLYBEAR-SOURCES.json)均保留。圖片來源與署名可在封面資訊中查看。

## English Summary

This unofficial community update is based on NitLink 1.2.5. It adds Japanese and Korean, improves localized game titles, capture-card selection, source-frame handling and resize recovery, and retains HolyBear game-library, Discord, HDR, VRR and audio features. GC553Pro and Switch 2 fixes have completed real-hardware acceptance. Discord artwork display still depends on the Discord client and its image proxy.
