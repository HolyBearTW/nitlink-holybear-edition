# CN_115 音訊可靠性移植

來源為 @CN115 的 [音訊可靠性分支](https://github.com/CN115/nitlink-lowlatency/tree/6829db62d5a37d7abc05f073065221566267e1ce)，
2026-11-03 查核時最新兩筆提交：

- `4269c6dfd1526caf66926b9ac8730c1522c39520`：分級復原、時鐘診斷、區塊音訊處理與紀錄重播。
- `6829db62d5a37d7abc05f073065221566267e1ce`：原生管線重播驗證、擷取錯誤復原與時鐘狀態顯示。

比對基準為 `19c011c235cb3457bfa54c789516205c3e6e9fc0`。
HolyBear 原有 `src/audio` 與此基準一致，因此以下功能原先均未具備。
本次精準套用音訊差異，沒有合併整條分支或其 CI、品牌與發版工作流程。

## 新增行為

| 項目 | 移植內容 |
| --- | --- |
| 復原 | 先重新填充本地 FIFO，再視持續缺損重建播放端或兩端；擷取停滯有獨立監測，沒有 HDMI 音訊時避免反覆重開。 |
| 時鐘 | 使用可信度加權估計；分別顯示學習中、有效、暫用與不可用狀態；重設裝置時重新學習。 |
| 音訊處理 | 改為區塊解碼並保留既有 sinc 係數及跨區塊相位；音量、靜音與音訊恢復使用約 1 ms 平滑過渡。 |
| 直通 | 同取樣率且關閉漂移補償時略過 FIR，仍保留格式、聲道與音量處理。 |
| 診斷 | 區分等待、啟動、播放、重新填充、擷取停滯及復原狀態；各自統計欠載、啟動靜音、重新緩衝與重設丟棄。 |
| 匯出 | 音訊診斷內提供 CSV 匯出，由原生儲存視窗選擇位置；紀錄只有數值事件，不含音訊取樣或裝置識別資料。 |
| 重播 | `NitLinkAudioReplay.exe trace.csv` 使用同一套控制邏輯核對紀錄；缺少事件或截斷的紀錄會明確標示無法驗證的決策。 |

保留 HolyBear 的 FIFO／共用播放佇列預設 **12／10 ms**、既有偏好、獨占模式與診斷預設關閉。
診斷重新整理的請求編號與完成回饋也保留。

音訊實作、工具與相關原始測試共 15 個檔案與來源提交逐一核對 blob 相符。
原生設定接線與儲存視窗擁有者則配合 HolyBear 的獨立設定視窗；
單次使用的播放狀態名稱映射放在序列化處就地處理。
介面／原生新增文案補齊英文、繁中、簡中、德文、韓文、日文。
來源提交及 MIT 授權記錄於 `SOURCE.txt`、`FEATURE_COMMITS.tsv`、`HOLYBEAR-SOURCES.json`。

## 本次驗證

只編譯音訊測試、重播工具及隔離的 WebView 測試宿主；主程式接線使用 `/Zs` 語法檢查，沒有產生新的驗收主程式。
完整回歸、發版、tag 與推送維持暫停。

以下 23 項 CTest 最終均通過：

- 8 項音訊／原生整合檢查：`audio_processing`、`audio_trace_replay`、`audio_trace_cli`、`audio_native_conversion`、`audio_clock_recovery`、`audio_packet_drift`、`audio_clock_drift`、`media_metadata_security`。
- 5 項設定檢查：`settings_security`、`webview_runtime_security`、`webview_settings_layout`、`webview_f1_visibility`、`webview_missing_assets`。
- 其餘 10 項 Smoke；已通過的 8 項 Smoke 沿用本次結果，沒有重複執行。

原生設定配置測試首次因新增說明按鈕改變 Tab 順序而失敗；補上該按鈕的焦點檢查，
並保留原有重新整理的焦點與空白鍵斷言後，單獨重跑通過。
另通過六語系 645 個介面字串／106 個原生字串校驗、JavaScript 語法、封裝腳本語法、隱私清單與差異格式檢查。
音訊處理效能量測在相同 FIR 的三組格式上平均約快 4.2～5.6 倍。

本機紀錄位於 `out/cn115-audio-focused-tests-20261103.log`、
`out/cn115-audio-layout-rerun-20261103.log`、`out/cn115-audio-remaining-smoke-20261103.log`、
`out/cn115-audio-application-syntax-20261103.log` 與 `out/cn115-audio-benchmark-20261103.log`。

原生整合測試以假 WASAPI 端點跑實際 AudioRouter，涵蓋擷取突發、非致命錯誤後的停滯復原、重採樣及紀錄重播。
實體擷取卡的 USB 送包節奏、長時間播放、可聽見的過渡、拔插與輸出裝置切換仍需實機驗收。
微型效能量測只反映指定音訊轉換段落，不等同整個程式的速度或端到端延遲。
