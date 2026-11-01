# NitLink 1.2.5.3 HolyBear Edition

**非官方社群整合版（Unofficial Community Edition）。**

此版以 1.2.5.2 HolyBear Edition 為基礎，修正封面與本機遊戲流程，並新增整合式標題列與模糊面板。

## 本次更新

- 視窗使用整合式標題列，模糊桌面背景；標題列保留獨立高度，不會覆蓋遊戲畫面。新增齒輪按鈕開啟 F1，以及圖示隨標準、詳細、精簡模式切換的 HUD 按鈕。全螢幕與子母畫面不顯示標題列。
- F1 面板新增預設的「模糊玻璃」背景，模糊面板後方的遊戲畫面，文字與操作按鈕保持清楚。可切回「傳統透明」，兩種模式分別保存不透明度。
- 官方素材合併時保留已取得的其他候選，不因商店搜尋已有橫圖而略過原有的直式或正方形素材；平台未回傳的直式圖片仍不會憑空產生。
- 修正連動遊戲已有官方封面、但尚未對應封面資料庫時，社群圖片與所有來源顯示沒有結果的問題。現在會先搜尋對應遊戲，再載入社群封面；Nintendo、PlayStation 與 Xbox 使用相同流程。
- 完成資料庫對應後，保留使用者選擇的封面來源與圖片方向。體驗版及 Switch 2 版本可搜尋對應原作；有多個符合結果時，由使用者選擇。
- 對應時一併比對平台與已保存的不同語言名稱，避免繁中平台名稱與社群目錄使用不同譯名時無法確認同一款遊戲。
- 切換封面來源時立即顯示 NitLink 載入圖示及對應狀態，逐張載入時顯示進度；需手動選擇對應遊戲時會明確提示，避免等待期間看起來沒有反應或過早顯示沒有結果。
- 修正本機遊戲按下「清除遊戲」後仍留在遊戲庫的問題，清除後會立即更新列表，重新啟動也不會恢復。已連動平台的遊戲仍保留於帳號遊戲庫，個別遊戲的畫質設定也會保留。

## 測試與驗證

- 依本次驗收要求，最新視窗與模糊面板改動僅編譯，不執行 CTest 或回歸測試；不將先前版本的測試結果列為這批改動通過。
- 先前社群對應與本機清除已有 Smoke／受影響功能及原生 WebView 測試紀錄，但完整 Full CTest 未完成。這些歷史結果不涵蓋本次新增的標題列、遊戲顯示子視窗與模糊效果。
- 仍須人工確認標題列桌面模糊、F1 遊戲模糊、左右／全版面板、不透明度、齒輪與三種 HUD 圖示，以及視窗縮放、DPI、全螢幕、子母畫面與 HDR／SDR 顯示。
- GC553Pro／Switch 2 的既有擷取修正已完成真實實機驗收；本次保留既有擷取行為，帳號與圖片功能仍須以驗收版確認實際服務回應。
- 自動模式無法取得更新率時，維持既有的 30 Hz fallback。

## 已知限制

- 桌面模糊效果需要支援系統背景材質的 Windows 11；不支援的系統會使用深色標題列。Windows 關閉透明效果時，系統材質可能顯示為實色。
- Nintendo Play Activity 的遊戲圖示與台灣搜尋的橫式主圖不保證提供直式包裝圖；官方商品頁有直式圖，也不代表這兩個介面會回傳它。
- 社群資料庫未收錄、平台未提供圖片或服務暫時無法回應時，仍可能沒有可用候選；無法唯一對應的遊戲需由使用者選擇搜尋結果。
- 清除連動平台的目前遊戲只取消選取，保留帳號遊戲庫與遊玩紀錄。
- 本機驗收包不會建立正式標籤或發布 GitHub Release；打包排除私人設定、帳號憑證、日誌與登入資料。

## 來源、授權與致謝

- 原始專案：@nitlink-dev，[nitlink-dev/nitlink](https://github.com/nitlink-dev/nitlink)；整合與維護：@HolyBearTW。
- 保留 @CN115 的音訊工作、@n810K 等貢獻者工作，以及 @Noka123 與 @i0rph 的翻譯貢獻。
- 保留 [MIT 授權](https://github.com/HolyBearTW/nitlink-holybear-edition/blob/holybear/integrated/LICENSE)、[第三方授權](https://github.com/HolyBearTW/nitlink-holybear-edition/blob/holybear/integrated/LICENSES.md)與[來源紀錄](https://github.com/HolyBearTW/nitlink-holybear-edition/blob/holybear/integrated/HOLYBEAR-SOURCES.json)。

## English Summary

This version adds a desktop-blurred integrated title bar with Settings and three-mode HUD controls, plus a game-blurred F1 background with a classic transparency option. It also improves community artwork lookup, loading feedback, preservation of official candidates and clearing local games. The latest UI changes are compiled for acceptance only; CTest and regression tests were not run for this revision at the user's request. Earlier test results do not validate the new window or glass rendering.
