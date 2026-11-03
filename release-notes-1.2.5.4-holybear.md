# NitLink 1.2.5.4 HolyBear Edition

**非官方社群整合版（Unofficial Community Edition）。**

此為預定發版更新日誌。完成本次驗收後再更新程式版本號；目前程式仍回報 1.2.5.3-holybear。

## 本次更新

- 設定視窗不會在程式啟動時自行開啟；新視窗首次啟動時置中，之後記住上次一般視窗位置。啟動時將擷取裝置與 Discord 初始化移出畫面首次呈現路徑，減少視窗出現後短暫無回應；不改變擷取格式、HDR 或音訊預設值。
- 新增整合式更新流程：在「關於」頁檢視以 Markdown 撰寫的更新日誌、下載進度及安裝狀態；下載後由主程式驗證檔案大小與 SHA-256，完成後可直接啟動安裝器，沿用安裝位置並保留設定與連動資料。齒輪會提示新版本，關閉自動檢查時也會隱藏提示；新設定預設開啟自動檢查。
- 關於頁的七位貢獻者會在資訊卡開啟時直接從 GitHub 取得頭像，並顯示各自的貢獻內容；載入失敗時使用姓名縮寫。資訊卡沿用既有個人頁連結及順序，上游作者列在第一位。
- 改善玻璃介面的捲軸、主視窗與設定視窗焦點切換、拖曳及最小化／還原處理；修正關閉設定視窗時的閃爍，以及左、右停駐面板分隔線與 NitLink 標誌顯示。經典樣式維持原有外觀。
- 移植並整合音訊時鐘追蹤、區塊重取樣、增益平滑及分級恢復流程；新增可關閉的診斷追蹤、CSV 匯出與離線重播工具。診斷畫面改為分組排版、精簡按鈕與文字，修正段落縮排並移除尾端分隔線。原有 12 ms 擷取 FIFO 與 10 ms 播放佇列目標不變。
- 修正音訊端點資訊、更新狀態及設定頁面版面細節；德文更新說明補上缺漏的句號。
- 發版建置流程會驗證下載的安裝程式簽章，並納入音訊重播工具及其說明文件。

## 測試與驗證

- JavaScript 語法及六語系檢查通過；五項設定與 WebView 必要檢查通過。原生 WebView 在六種語言下完成 42 次貢獻者頭像載入檢查，七張資訊卡截圖均已產生，另確認失敗時會顯示姓名縮寫。
- 完整 CTest 共 63 項：首輪 62 項通過，唯一失敗是標籤安全測試夾具未複製新增的更新器腳本；補齊夾具後該測試單獨重跑通過。兩輪合計 63 項皆有通過記錄；為避免重跑未受影響的測試，未再次執行完整套件。
- Release 全目標建置通過。程式版本仍為 1.2.5.3-holybear；本次尚未更新版本號、建立發版標籤或發布套件。
- 此更新日誌不代表已發布；目前程式版本仍為 1.2.5.3-holybear。

## 已知限制

- GitHub 頭像需要網路連線；若服務無法回應，貢獻者資訊卡會顯示姓名縮寫。
- 玻璃材質依 Windows 透明效果與桌面合成器狀態呈現；自動化測試無法代表所有顯示卡及桌面設定下的實際像素效果。
- 音訊時鐘追蹤及重播工具不會改變擷取裝置、HDR 或視訊時序設定。

## 來源、授權與致謝

- 原始專案：@nitlink-dev，[nitlink-dev/nitlink](https://github.com/nitlink-dev/nitlink)；整合與維護：@HolyBearTW。
- 音訊可靠性功能移植自 @CN115 的 [NitLink 低延遲音訊分支](https://github.com/CN115/nitlink-lowlatency)；並保留其來源及提交紀錄。
- 安裝器、玻璃介面及動畫參考 @tavricccc 的 [Harbor](https://github.com/tavricccc/harbor)，並依原授權保留來源聲明。
- 感謝 @n810K、@Noka123、@i0rph 及其他貢獻者的程式與翻譯工作。各作者貢獻內容見「關於」頁。
- 保留 [MIT 授權](https://github.com/HolyBearTW/nitlink-holybear-edition/blob/holybear/integrated/LICENSE)、[第三方授權](https://github.com/HolyBearTW/nitlink-holybear-edition/blob/holybear/integrated/LICENSES.md)與[來源紀錄](https://github.com/HolyBearTW/nitlink-holybear-edition/blob/holybear/integrated/HOLYBEAR-SOURCES.json)。

## English Summary

This planned update improves startup responsiveness and window-position restoration, adds an in-app Markdown updater with package integrity checks, and loads contributor avatars directly from GitHub with initials as a fallback. It fixes glass-window focus, movement, restore and closing behavior, improves audio clock recovery and diagnostics, and updates packaging checks. JavaScript, six-locale, native WebView and all 63 CTest cases have passing evidence; the tag-security fixture was fixed and rerun separately after its initial omission. The application version remains 1.2.5.3 until the release is approved.
