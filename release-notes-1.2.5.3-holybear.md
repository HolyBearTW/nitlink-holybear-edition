# NitLink 1.2.5.3 HolyBear Edition

**非官方社群整合版（Unofficial Community Edition）。**

此版以 1.2.5.2 HolyBear Edition 為基礎，修正封面與本機遊戲流程，並新增整合式標題列與模糊面板。

## 本次更新

- 安裝程式直接移植 Harbor 的獨立 WinUI 介面，改用 NitLink Logo、淺藍品牌色及六種語言字型；提供安裝、更新、進度、完成、失敗及解除安裝畫面。偵測到已安裝時顯示「更新 NitLink」，沿用安裝位置與桌面捷徑選項。更新前會先請該安裝的 NitLink 儲存並結束。
- 全新安裝與重新安裝可輸入安裝位置或按「瀏覽」選擇資料夾；解除安裝後可重新選回原來的位置，保留該位置內的設定與連動資料。更新時沿用已偵測到的安裝位置。
- 安裝、更新、解除安裝及「授權與原始碼」視窗移植 Harbor 主程式的自訂桌面玻璃與動畫，以 NitLink 品牌色呈現；套用主程式已修正的移動、失焦、最小化／還原、顯示同步及圓角邊線處理。授權全文可捲動、選取文字，關閉後回到安裝器。全新安裝預設使用目前使用者的 `%LOCALAPPDATA%\Programs\NitLink HolyBear Edition`，更新仍沿用原安裝位置。
- 安裝與解除安裝沿用 NitLink 的 NSIS 引擎，保留設定、連動帳號、擷取圖片及其他使用者檔案；Windows 10、靜默操作及無法啟動 WinUI 執行環境時保留既有安裝介面。獨立 WinUI 安裝介面採 GPLv3，完整對應原始碼隨套件附於 `licenses/setup-ui-source.zip`，主程式維持 MIT 授權。
- 視窗與遊戲容器在建立時設定合成背景，避免事後設定未生效而保留不透明底層；停用自訂標題列下方的系統邊框繪製，並修正圓角裁切多出一像素的範圍。最大化還原後會重新顯示獨立玻璃層。
- 標題列按鈕改為 28px 圓角操作區，統一細線圖示、齒輪及 HUD 的尺寸與間距，依 DPI 縮放。
- 視窗使用整合式標題列，模糊桌面背景；標題列保留獨立高度，不會覆蓋遊戲畫面。新增齒輪按鈕開啟 F1，以及圖示隨標準、詳細、精簡模式切換的 HUD 按鈕。全螢幕與子母畫面不顯示標題列。
- 「玻璃模糊」讓標題列、F1 與無訊號畫面共用連續的桌面玻璃，移除標題列底線與 F1 頂部的整片深色底色，避免材質被分割。文字與操作按鈕保持清楚。
- 玻璃模式只顯示「模糊程度」，同步調整主視窗與設定視窗。預設 0% 保留原本 Windows 系統模糊，往上調整會增加模糊，100% 為額外模糊的最大值；不將系統效果宣稱為可讀取的固定強度。面板額外底色不透明度固定為 0%，不另顯示設定。
- 程式啟動時不自動開啟 F1 設定，首次使用或沒有設定檔時亦同；僅在使用者按 F1 或齒輪後顯示。
- 獨立設定視窗的自訂標題列只顯示 NitLink 字標，不再重複下方的「設定」標題；工作列標題與齒輪提示只顯示「設定」，不寫死可自訂的 F1 快捷鍵。快捷鍵頁主標題與 HUD 外觀副標題之間增加間距。
- 面板位置新增預設的「獨立視窗」，使用 FULL 排布，支援拖曳、縮放、最小化、最大化與還原，沿用相同的玻璃及視窗動畫。F1 與齒輪開關同一個設定視窗，關閉設定時遊戲主視窗繼續運作；切換至靠左、靠右或 FULL 時，保留既有設定頁內容。已保存的面板位置仍會保留。
- 無訊號時使用桌面玻璃背景與透明提示卡；訊號恢復後顯示正常遊戲畫面。已選擇的自訂無訊號圖片保留原有呈現方式。
- 修正主機睡眠或關機、已確認訊號遺失後，視窗標題仍顯示最後一次 HDR 狀態的問題。訊號遺失時隱藏標題的 HDR、VRR、解析度與更新率，保留遊戲及主機名稱；有效畫面恢復後自動更新。短暫重新同步仍保留最後確認的資料，不改動 HDR 輸出與擷取設定。
- 所有玻璃模糊效果僅在「玻璃模糊」模式生效。切回「經典樣式」會停用標題列、設定及無訊號畫面的玻璃模糊，並保留模糊程度供下次切回使用；經典樣式可調整整個面板的不透明度。
- 獨立視窗及 FULL 的訊號資訊欄移除頂部框線，右側改為上下留白、兩端漸淡的細線，減少整塊框住的感覺。
- 玻璃模式的靠左／靠右面板移除硬邊框，設定列改用縮排、漸淡的分隔線；窄版導覽列恢復 NitLink 小 Logo，保留五個分頁。
- 修正設定控制項建立期間切換顯示視窗時，獨立視窗空白而內容留在主視窗的問題；完成建立後會套用當前選擇的視窗。
- 主視窗與獨立設定視窗各自保留頂層工作列視窗，玻璃層緊貼對應視窗後方；移動、失焦及最小化／還原時同步維持玻璃顯示，獨立設定視窗可在 NitLink 工作列群組切換。齒輪再次按下會關閉已顯示的設定視窗；隱藏或最小化後可重新開啟。
- 標題列及控制按鈕統一由 NitLink 繪製，移除另一套原生標題列，避免啟動或還原時重複出現；最小化、還原、最大化與關閉仍呼叫系統視窗操作。
- 標題列使用透明 NitLink 字標，來源與遊戲資訊不再重複顯示 NitLink 名稱；標題資訊依 F1 的有效語言選用對應字型，包含「系統預設」解析後的語言。繁中使用微軟正黑體，字級為 15px，標題改用粗體；其他語言同樣使用各自字型的粗體。Archivo 字型與字標內嵌於執行檔。
- F1 與遊戲畫面使用同一個外層視窗的不同呈現區域，玻璃區域不再被不透明遊戲底層遮住。標題列保留獨立高度，不覆蓋遊戲；設定頁切換視窗時不重新載入。
- 視窗動畫採 Harbor 的玻璃／內容分層方式與曲線：玻璃從 0.94 倍彈入，縮放 360ms、淡入 220ms，內容只淡入、不隨玻璃縮放；最小化與關閉時，玻璃縮小並與內容一起在 120ms 內淡出。最大化／還原先套用最終內容排版，再以 180ms 滑動圓角裁切邊界與玻璃，避免逐格重新排版。跟隨 Windows 系統動畫開關。
- 玻璃層獨立繪製桌面模糊、飽和度、染色、邊緣高光及陰影。外框高光改畫在內容視窗外緣，使左、右邊線一致。最小化時隱藏玻璃；全螢幕及子母畫面沒有標題列時，僅在顯示玻璃設定或玻璃無訊號提示時保留玻璃。
- 官方素材合併時保留已取得的其他候選，不因商店搜尋已有橫圖而略過原有的直式或正方形素材；平台未回傳的直式圖片仍不會憑空產生。
- 修正連動遊戲已有官方封面、但尚未對應封面資料庫時，社群圖片與所有來源顯示沒有結果的問題。現在會先搜尋對應遊戲，再載入社群封面；Nintendo、PlayStation 與 Xbox 使用相同流程。
- 遊戲 API 暫時連線失敗、逾時、限流或回傳 5xx 時，背景搜尋與社群封面請求會短暫等待後自動重試一次。仍失敗時顯示服務狀態，不把帳號遊戲卡片當成已找到社群封面，也不誤報「查無結果」。
- 搜尋到多個完整名稱相符的遊戲時，預設自動對應普通版封面資料庫，並保留「對應封面資料庫」供使用者調整；名稱無法確認時才提示手動選擇。體驗版及 Switch 2 版本仍可搜尋對應原作。Nintendo 商店封面及連動封面合併時區分普通版與 Switch 2 版，避免兩個搜尋結果共用 Switch 2 圖片與標題。
- 對應時一併比對平台與已保存的不同語言名稱，避免繁中平台名稱與社群目錄使用不同譯名時無法確認同一款遊戲。
- 切換封面來源時立即顯示 NitLink 載入圖示及對應狀態，逐張載入時顯示進度；需手動選擇對應遊戲時會明確提示，避免等待期間看起來沒有反應或過早顯示沒有結果。
- 修正本機遊戲按下「清除遊戲」後仍留在遊戲庫的問題，清除後會立即更新列表，重新啟動也不會恢復。已連動平台的遊戲仍保留於帳號遊戲庫，個別遊戲的畫質設定也會保留。

## 測試與驗證

- JavaScript 語法及六種語系靜態檢查通過；五項封裝設定頁安全測試、遊戲搜尋對應及原生視窗狀態測試通過。視窗測試在隔離桌面確認主視窗與獨立設定視窗的玻璃層跟隨移動、失焦及最小化／還原，並確認兩者為頂層工作列視窗。
- 最新驗收程式的完整無篩選 CTest 已通過 60／60，失敗與跳過均為 0；包含標題訊號修正的回歸測試，另已通過相關測試 5／5、Smoke 15／15 及六語系檢查。實際主機睡眠／喚醒與畫面仍待使用者驗收；自動化視窗測試不代表所有 Windows 桌面、顯示卡與 HDR 模式的像素結果相同。
- 獨立安裝器另通過安裝／更新判斷、六語系、系統語言解析、含空白與中文的路徑、桌面捷徑選項及引擎失敗回報檢查；以隔離目錄、登錄項及捷徑名稱完成實際安裝、更新與解除安裝，核對所有檔案雜湊及設定、帳號資料保留。Windows 10 與缺少執行環境的實際畫面仍須在對應系統確認。
- 玻璃安裝器的安裝、更新、解除安裝畫面各通過 178 項隔離桌面檢查，合計 534 項；包含四輪移動、失焦、最小化／還原、隱藏／顯示，以及實際授權玻璃視窗的開啟、移動、關閉與安裝器恢復操作。檢查實際視窗可見性、前後順序、位置、材質與內容範圍；不將這些結構檢查宣稱為所有 Windows 桌面的像素保證。
- 仍須人工確認連續桌面玻璃、連動模糊程度、獨立設定視窗、無訊號玻璃、左右／全版面板、經典樣式模式、字標與語系字型、齒輪與三種 HUD 圖示，以及開啟／最小化／最大化／還原動畫、視窗縮放、DPI、全螢幕、子母畫面與 HDR／SDR 顯示。
- GC553Pro／Switch 2 的既有擷取修正已完成真實實機驗收；本次保留既有擷取行為，帳號與圖片功能仍須以驗收版確認實際服務回應。
- 自動模式無法取得更新率時，維持既有的 30 Hz fallback。

## 已知限制

- 桌面模糊效果需要支援系統背景材質的 Windows 11；不支援的系統會使用深色標題列。Windows 關閉透明效果時，系統材質可能顯示為實色。
- WinUI 安裝介面需要 Windows 11、.NET 10 及 Windows App Runtime。缺少執行環境時會顯示對應提示；無法啟動時改用原有安裝介面，NitLink 主程式不新增 .NET 相依性。
- 動畫使用 Harbor 相同的分層方式、時間與 Bézier 曲線，以 NitLink 原生 Windows Composition 介面實作；保留遊戲的 HWND swap chain，沒有加入 WinUI 執行階段。實際動畫、連動桌面玻璃、獨立設定視窗及無訊號玻璃仍待此驗收包人工確認。
- Nintendo Play Activity 的遊戲圖示與台灣搜尋的橫式主圖不保證提供直式包裝圖；官方商品頁有直式圖，也不代表這兩個介面會回傳它。
- 社群資料庫未收錄、平台未提供圖片或服務暫時無法回應時，仍可能沒有可用候選；沒有完整名稱相符結果的遊戲需由使用者選擇搜尋結果。
- 清除連動平台的目前遊戲只取消選取，保留帳號遊戲庫與遊玩紀錄。
- 本機驗收包不會建立正式標籤或發布 GitHub Release；打包排除私人設定、帳號憑證、日誌與登入資料。

## 來源、授權與致謝

- 原始專案：@nitlink-dev，[nitlink-dev/nitlink](https://github.com/nitlink-dev/nitlink)；整合與維護：@HolyBearTW。
- 透明標題字標來自原始專案的 [NitLink lockup](https://github.com/nitlink-dev/nitlink/blob/77d87b40248867b82cd56a0fff4e32314834e314/docs/images/nitlink-lockup.svg)，保留原始 SVG，原生介面使用透明 PNG 呈現。
- 主程式玻璃層與動畫行為參考 @tavricccc 的 [Harbor GlassWindow](https://github.com/tavricccc/harbor/blob/a56ab11d48fd654f3131ce0ec115ad50d6e5e30f/src/Harbor/Glass/GlassWindow.cs)、[GlassSurface](https://github.com/tavricccc/harbor/blob/a56ab11d48fd654f3131ce0ec115ad50d6e5e30f/src/Harbor/Glass/GlassSurface.cs) 與 [Motion](https://github.com/tavricccc/harbor/blob/a56ab11d48fd654f3131ce0ec115ad50d6e5e30f/src/Harbor/Controls/Motion.cs) 的設計及參數，以 Windows 公開 API 實作。獨立安裝介面另外移植 [HarborSetup](https://github.com/tavricccc/harbor/tree/2c243bbbaa1bba4f412b6082964ba03eca64b84a/src/HarborSetup) 原碼，保留 GPLv3 授權與來源說明。
- 保留 @CN115 的音訊工作、@n810K 等貢獻者工作，以及 @Noka123 與 @i0rph 的翻譯貢獻。
- 保留 [MIT 授權](https://github.com/HolyBearTW/nitlink-holybear-edition/blob/holybear/integrated/LICENSE)、[第三方授權](https://github.com/HolyBearTW/nitlink-holybear-edition/blob/holybear/integrated/LICENSES.md)與[來源紀錄](https://github.com/HolyBearTW/nitlink-holybear-edition/blob/holybear/integrated/HOLYBEAR-SOURCES.json)。

## English Summary

This version adds a desktop-blurred integrated title bar with Settings and three-mode HUD controls, plus a shared desktop-glass F1 background with linked blur strength, an independent settings window using the Full layout, a glass No Signal screen and a classic style option. It improves community artwork lookup, automatic matching, edition-specific covers, automatic retries, loading feedback, and clearing local games. The independent GPLv3 WinUI setup uses Harbor's custom glass host for setup and license windows, with NitLink branding and six languages; Windows 10 and silent operations retain the NSIS interface. Installer source accompanies the payload. Confirmed input loss hides cached signal details in the title until real video returns, while preserving metadata through short resyncs; localized titles use bold text. The latest native acceptance build passed the full unfiltered CTest suite 60/60, and setup glass lifecycle checks passed 534 assertions on isolated desktops. Engine handoff and isolated install/upgrade/uninstall checks also passed. User visual and sleep/wake acceptance remain pending.
