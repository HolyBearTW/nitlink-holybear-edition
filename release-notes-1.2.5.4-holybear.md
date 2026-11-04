# NitLink 1.2.5.4 HolyBear Edition

**非官方社群整合版（Unofficial Community Edition）。**

此版本整合設定、更新、音訊診斷與關於頁改進。

## 本次更新

- 設定視窗不會在程式啟動時自行開啟；新視窗首次啟動時置中，之後記住上次一般視窗位置。啟動時將擷取裝置與 Discord 初始化移出畫面首次呈現路徑，減少視窗出現後短暫無回應；不改變擷取格式、HDR 或音訊預設值。
- 新增整合式更新流程：在「關於」頁檢視以 Markdown 撰寫的更新日誌、下載進度及安裝狀態；下載後由主程式驗證檔案大小與 SHA-256，完成後可直接啟動安裝器，沿用安裝位置並保留設定與連動資料。齒輪會提示新版本，關閉自動檢查時也會隱藏提示；新設定預設開啟自動檢查。
- 關於頁的七位貢獻者會在資訊卡開啟時直接從 GitHub 取得頭像，並顯示各自的貢獻內容；載入失敗時使用姓名縮寫。資訊卡沿用既有個人頁連結及順序，上游作者列在第一位。
- 關於頁新增官方網站與支持連結，並補齊這些網址及授權內文來源連結的原生白名單。「專案授權」、「第三方授權」與「致謝名單」改為程式內的獨立文件視窗，沿用設定視窗的原生玻璃、拖曳、縮放與開關動畫；玻璃效果跟隨面板樣式及模糊程度，開關文件時不再切換設定頁內的模態背景。文件視窗移除底部重複的關閉按鈕與分隔線，保留頂端關閉操作。
- 三份文件開啟時自動從 GitHub 整合分支同步，以 Markdown 轉換並安全顯示；下載在背景執行，失敗時顯示內建版本與來源狀態。文件切換與關閉會取消未完成的下載，背景同步不會搶走視窗焦點。
- 右上角「提供意見」改連到 HolyBear Edition 的 GitHub Issues；「支持聖小熊」連到 Buy Me a Coffee。技術資訊中的原回饋入口改名為「提供意見給原作者」，保留原網址；新增網址及三份線上文件來源皆納入固定白名單。
- 修正獨立設定視窗下緣及下方兩角無法拖曳縮放：設定內容保留原生縮放邊界，避免 WebView 子視窗攔截滑鼠操作。經典樣式與玻璃模糊皆適用。
- 改善玻璃介面的捲軸、主視窗與設定視窗焦點切換、拖曳及最小化／還原處理；修正關閉設定視窗時的閃爍，以及左、右停駐面板分隔線與 NitLink 標誌顯示。經典樣式維持原有外觀。
- 移植並整合音訊時鐘追蹤、區塊重取樣、增益平滑及分級恢復流程；新增可關閉的診斷追蹤、CSV 匯出與離線重播工具。診斷畫面改為分組排版、精簡按鈕與文字，修正段落縮排並移除尾端分隔線。原有 12 ms 擷取 FIFO 與 10 ms 播放佇列目標不變。
- 修正音訊端點資訊、更新狀態及設定頁面版面細節；德文更新說明補上缺漏的句號。
- 發版建置流程會驗證下載的安裝程式簽章，並納入音訊重播工具及其說明文件。

## 測試與驗證

- 既有 GC553Pro／Switch 2 遊戲與擷取流程已完成真實實機驗收；本版文件同步、連結與視窗邊界另以自動化檢查及實際網路請求確認。
- 乾淨目錄 x64 Release 建置通過；最終完整、不篩選的 CTest 為 63/63，失敗 0 項、略過 0 項。發版文件與簽章說明的 65 項檢查通過。
- JavaScript 語法及六語系檢查通過，涵蓋 651 個選單字串與 106 個原生字串；五項設定與 WebView 必要檢查均通過。
- 文件同步及連結修改通過七項針對性 CTest（更新器、設定安全、WebView 安全、設定版面、F1 可見性、缺少資源與視窗生命週期），並確認 46 個顯示連結與三份線上文件來源符合白名單。三份文件皆以實際 WinHTTP 請求成功讀取；縮放檢查涵蓋經典與玻璃樣式的三個下緣位置。
- 安裝器後端 40 項檢查、三種安裝器畫面共 534 項玻璃生命週期斷言，以及原生自動更新交接檢查均通過。確認更新會沿用含中文的自訂路徑，只重新啟動一次，並保留設定及帳號資料。
- 程式、Windows 檔案資訊、HUD、關於頁與安裝器版本統一升級為 1.2.5.4-holybear。

## 已知限制

- 當自動模式無法取得更新率時，仍使用保守的 30 Hz fallback；這只適用於自動模式，手動上限與關閉上限維持原有設定。
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

This update improves startup responsiveness and window-position restoration, adds an in-app Markdown updater with package integrity checks, and loads contributor avatars directly from GitHub with initials as a fallback. License and acknowledgment documents open in a native glass window with the existing animations and synchronize Markdown from GitHub in the background, retaining bundled documents when offline. Feedback and support links distinguish HolyBear Edition from the original author and use explicit native destinations. Settings now preserve native bottom and corner resize grips outside WebView content. It also improves glass-window focus, movement, restoration and closing behavior, audio clock recovery and diagnostics, and packaging checks. The application and installer versions are 1.2.5.4-holybear.
