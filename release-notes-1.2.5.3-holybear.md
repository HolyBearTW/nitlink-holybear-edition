# NitLink 1.2.5.3 HolyBear Edition

**非官方社群整合版（Unofficial Community Edition）。**

此修補版以 1.2.5.2 HolyBear Edition 為基礎，修正社群封面對應與本機遊戲清除流程。

## 本次更新

- 修正連動遊戲已有官方封面、但尚未對應封面資料庫時，社群圖片與所有來源顯示沒有結果的問題。現在會先搜尋對應遊戲，再載入社群封面；Nintendo、PlayStation 與 Xbox 使用相同流程。
- 完成資料庫對應後，保留使用者選擇的封面來源與圖片方向。體驗版及 Switch 2 版本可搜尋對應原作；有多個符合結果時，由使用者選擇。
- 修正本機遊戲按下「清除遊戲」後仍留在遊戲庫的問題，清除後會立即更新列表，重新啟動也不會恢復。已連動平台的遊戲仍保留於帳號遊戲庫，個別遊戲的畫質設定也會保留。

## 測試與驗證

- 本次驗收執行 Smoke 與受影響功能的必要測試，未完成整套 Full CTest；各項結果與測試範圍隨驗收包記錄，不將未執行的測試列為通過。
- 社群對應的原生 WebView 測試涵蓋三平台、體驗版、Switch 2 版本、來源與方向保留，以及拒絕自動選用不相關或不唯一的結果。本機清除測試包含重新載入設定後的遊戲庫狀態。
- GC553Pro／Switch 2 的既有擷取修正已完成真實實機驗收；本次保留既有擷取行為，帳號與圖片功能仍須以驗收版確認實際服務回應。
- 自動模式無法取得更新率時，維持既有的 30 Hz fallback。

## 已知限制

- 社群資料庫未收錄、平台未提供圖片或服務暫時無法回應時，仍可能沒有可用候選；無法唯一對應的遊戲需由使用者選擇搜尋結果。
- 清除連動平台的目前遊戲只取消選取，保留帳號遊戲庫與遊玩紀錄。
- 本機驗收包不會建立正式標籤或發布 GitHub Release；打包排除私人設定、帳號憑證、日誌與登入資料。

## 來源、授權與致謝

- 原始專案：@nitlink-dev，[nitlink-dev/nitlink](https://github.com/nitlink-dev/nitlink)；整合與維護：@HolyBearTW。
- 保留 @CN115 的音訊工作、@n810K 等貢獻者工作，以及 @Noka123 與 @i0rph 的翻譯貢獻。
- 保留 [MIT 授權](https://github.com/HolyBearTW/nitlink-holybear-edition/blob/holybear/integrated/LICENSE)、[第三方授權](https://github.com/HolyBearTW/nitlink-holybear-edition/blob/holybear/integrated/LICENSES.md)與[來源紀錄](https://github.com/HolyBearTW/nitlink-holybear-edition/blob/holybear/integrated/HOLYBEAR-SOURCES.json)。

## English Summary

This patch resolves missing catalogue mappings before loading community artwork for linked Nintendo, PlayStation and Xbox games, even when official artwork is already available. Source and shape selections survive mapping; demo and Switch 2 edition suffixes are handled without guessing unrelated matches. Clearing a local game removes its persisted library entry while retaining saved video settings and linked-account libraries. Acceptance validation covers Smoke and affected tests; a complete Full CTest run is not claimed.
