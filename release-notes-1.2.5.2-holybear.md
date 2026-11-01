# NitLink 1.2.5.2 HolyBear Edition

**非官方社群整合版（Unofficial Community Edition）。**

以 NitLink 1.2.5 為基礎，保留 HolyBear Edition 的擷取、畫面與音訊功能，新增主機帳號遊戲庫並改善封面選擇與 Discord 活動內容。

## 本次更新

- 快捷鍵可在設定中錄製自訂組合；儲存後立即套用並保留。只有未儲存且無衝突的變更可按「儲存」，配置與預設不同時可按「重設」還原草稿。原有按鍵維持為預設，輸入文字與錄製期間避免誤觸功能。
- Tab 可循環切換標準、詳細與透明背景的精簡文字 HUD。精簡模式預設使用綠色，可自訂文字顏色及 10–32px 字級，並自由組合內容 FPS、擷取 FPS、呈現 FPS、擷取耗時；選色、字級與顯示項目僅在精簡模式顯示，設定會保留。
- HUD 使用簡短指標名稱，詳細定義在滑鼠停留時顯示；擷取耗時限定為擷取卡到程式的傳遞時間，內容 FPS 為畫面變化估計。
- HUD 顯示模式包含「不顯示 HUD」，預設關閉時會正確反映；選擇標準、詳細或精簡模式會啟用 HUD，並與 HUD 開關快捷鍵同步。
- 修正 SDR 精簡 HUD 未帶入顯示項目、字級及顏色設定的問題，修改後會在下一次繪製套用，與 HDR 路徑一致。
- 快捷鍵標題旁以資訊按鈕提供操作說明與前景限制，「儲存／重設」移至標題列右側；修正靠左／靠右模式的列內留白。FULL 寬版以左右兩欄排列快捷鍵與 HUD 設定。
- 精簡 HUD 字級拉條統一使用子母畫面不透明度的控制項樣式，並調整顯示項目的左右留白與上下間距。
- 影像頁面的「面板位置」上方新增面板不透明度，範圍為 10–100%，預設 100%；拖曳時立即套用，關閉程式時保存。降低不透明度時，FULL 與靠左／靠右面板都可透出實際遊戲畫面。

- 支援 Nintendo Switch／Switch 2、PlayStation PS4／PS5、Xbox One／Series X|S 帳號連動，顯示已玩遊戲、平台提供的遊玩總時數與最近遊玩。
- 帳號連動區塊可手動收合；首次未連動時全部展開，已有連動帳號後，其他未連動平台預設收合。
- 遊戲名稱依目前介面語言顯示；系統預設會先解析實際語言。官方封面保留平台可提供的不同語言版本，優先選用目前介面語言，並合併相同圖片。
- 修正平台在繁中請求中仍回傳英文名稱時，錯誤跳過目錄補查的問題；公開目錄的通用中文名稱會依繁／簡中文介面轉換。
- 封面預設顯示官方素材，來源順序為官方素材、社群圖片、所有來源。搜尋縮圖優先使用官方正方形圖片；不同語言的官方素材以簡短語言標籤顯示，涵蓋正方形、直式與橫式。
- 官方封面資訊使用來源提供的語系，Nintendo、PlayStation、Xbox 的作者欄分別標示其官方來源。Nintendo 商店維持先查台灣、找不到再查香港，並依實際來源顯示名稱。
- 修正部分 Nintendo 官方圖片被拒絕下載的問題。遊戲庫先載入圖片，再補其他資料；公開資料庫無法回應時，仍可使用已連動帳號提供的遊戲與封面。
- 遊戲庫啟動時載入帳號封面，避免因目前主機來源不同而丟棄圖片結果；啟動期間切換來源時也會補載，不需先點選遊戲或搜尋。
- 體驗版查找封面時會移除 DEMO／體驗版字樣，保留原始帳號遊戲 ID 與遊玩紀錄。體驗版排在一般遊戲後方，不會因共用封面資料庫 ID 而與正式版混在一起。
- 遊戲庫縮圖統一為正方形，調整搜尋結果高度與按鈕文字置中，移除不明確的取消按鈕與重複錯誤提示。已連動目前主機來源時，不再顯示其他平台的連動提醒。
- 搜尋結果只保留目前語言的遊戲名稱，移除重複的原始名稱與多餘留白；靠左／靠右面板的正方形與直式封面每行三張，橫式維持每行兩張。
- 遊戲庫在靠左、靠右與展開模式維持每行兩款；封面資訊視窗對齊目前面板中央，移除靠邊固定造成的偏移。
- 三平台官方封面的商品名稱依每張圖片的來源語言顯示，封面候選下方文字與資訊視窗使用相同名稱。
- Nintendo 使用系統瀏覽器登入，選擇帳號並允許開啟 NitLink 後，自動接收授權回呼並同步 Play Activity。登入回呼使用此次連動的 `state` 與 PKCE，結束後移除此程式註冊的回呼入口；其他程式使用中的入口會保留。
- Sony 使用獨立的 Edge／Chrome 登入視窗，自動取得此次登入產生的 NPSSO 並同步 PS4／PS5 遊玩紀錄；登入視窗使用專用的暫存資料夾。Nintendo 與 Sony 都提供手動連動入口；Sony 可貼上 NPSSO 值或完整 JSON 回應。
- Xbox 按「開啟登入頁」後，在系統瀏覽器完成 Microsoft 登入即可自動連動並同步遊戲，不需填寫應用程式資訊或貼上授權碼。待登入可取消，五分鐘後逾時；授權碼與權杖不寫入日誌。
- 各平台連動視窗水平與垂直置中，標題搭配對應 Logo，分隔線與登入說明之間保留一致間距。
- 啟動時若尚未指定主機來源，從已連動帳號中選擇遊戲數量最多的平台，並依遊戲選擇對應主機；既有帳號與首次連動都適用。已手動指定的來源保留，自動選定後顯示三秒提示；遊戲庫總數上方增加間距。
- 自動切換主機來源的提示置於上方導覽列下方，避免遮住分頁按鈕。
- 官方商品以完整遊戲名稱及已知語言名稱比對，避免共用單字、版本字樣或續作前綴帶入其他遊戲。語言小標籤取代重複的來源語系文字。
- 靠左／靠右的 F1 面板可從內側邊緣拖曳調整寬度；預設與最小寬度為 500，讓繁體介面中的 Xbox 篩選按鈕維持在第一行。
- Discord 活動第二行只顯示「遊玩總時數」，不再加入解析度、FPS、HDR、VRR；本次遊玩計時保留，活動預覽與實際傳送內容共用名稱與時數資料。

## 測試與驗證

- 本機編譯包的測試範圍與執行情況記錄於 BUILD.txt；未執行的檢查不列為通過。交付前另核對包檔版本、內容清單與私人帳號資料。
- Nintendo Play Activity 的名稱與圖片已使用實際帳號資料檢查；Sony 授權交換、數字帳號 ID 的個人資料查詢及遊戲清單已取得正常 API 回應。三平台的自動登入、同步與關閉流程仍需在本次原生編譯包中人工驗證。
- 此版本的完整 CTest 結果、失敗與略過項目數由發布套件的建置驗證區塊及 BUILD.txt 記錄；驗收包與發布前套件的測試範圍分開標示。
- GC553Pro／Switch 2 的既有擷取修正已完成真實實機驗收；這不代表本次新增的帳號功能都經過相同實機驗收。
- 自動模式無法取得更新率時，仍維持既有的 30 Hz fallback。

## 已知限制

- 平台未提供某個語言、圖片或遊玩總時數時，保留可用的來源資料；不會產生不存在的圖片或時數。
- 平台連動使用其帳號與遊玩紀錄介面，實際可用資料仍受平台權限、更新頻率及服務回應影響。
- Discord 傳送資料可由測試確認；最終圖片與內容顯示仍取決於 Discord 客戶端。
- 本機測試包不會建立 Git 標籤或發布 GitHub Release。
- 打包清單排除個人設定、連動憑證、帳號匯出、日誌與 WebView 登入資料；交付前另外掃描測試包中的私人帳號資料。

## 來源、授權與致謝

- 原始專案：@nitlink-dev，[nitlink-dev/nitlink](https://github.com/nitlink-dev/nitlink)。
- 整合與維護：@HolyBearTW；保留 @CN115 的低延遲音訊、@n810K 等貢獻者工作，以及 @Noka123 與 @i0rph 的翻譯貢獻。
- 保留 [MIT 授權](https://github.com/HolyBearTW/nitlink-holybear-edition/blob/holybear/integrated/LICENSE)、[第三方授權](https://github.com/HolyBearTW/nitlink-holybear-edition/blob/holybear/integrated/LICENSES.md)與[來源紀錄](https://github.com/HolyBearTW/nitlink-holybear-edition/blob/holybear/integrated/HOLYBEAR-SOURCES.json)。平台圖示的授權與署名隨測試包保留。

## English Summary

This unofficial community update introduces console account linking, game libraries, localized names and official artwork variants. Nintendo sign-in returns from the system browser through a temporary native callback. Sony sign-in uses a dedicated Edge/Chrome window with an isolated profile to receive the NPSSO session. Nintendo and Sony also offer manual linking. Xbox uses the system browser and PKCE, with no application settings required from users. The update includes demo artwork lookup, square thumbnails, resizable docked panels and panel opacity. Discord uses total playtime while preserving the session timer. Shortcuts can be customized and saved. The HUD offers Off, Standard, Detailed and Compact modes, with configurable compact metrics, green default text, color and font size. Appearance preferences apply in SDR and HDR. Build-specific CTest results are recorded in the generated verification section; automated coverage does not replace live platform sign-in and Discord client checks.
