# my-mikanOS
本リポジトリは、『ゼロからのOS自作入門』（内田公太 (2021)、マイナビ出版）を参考にして作った自作 OS を拡張したものを載せています。
現在進行形で拡張したりしなかったりします。

# 拡張内容
## メモリの管理
mikanOS では物理メモリの管理を 4KiB ごとに bitmap で行っています。また、メモリのアロケート・ディアロケートを基本的に 1 ページずつ走査して行っています。

最終的に Hugepage を透過的に割り当てることをめざし、メモリ管理の粒度を複数種類にします。Linux のバディアロケータは 4MiB までの粒度で細かく管理していますが、まずは 2MiB の粒度だけ追加しています。

結果の比較として、適当なアプリの実行時間を比較します。なお、アプリの実装も併せて変えたらだいぶ早くなってしまい、カーネル単体の影響がわからなくなっています。。。
- アプリ内容：ファイルを読み込み、コマンドで渡した引数がそのファイル内に何回出現するか調べる
- 結果：


改造前の実行時間(elapsed=...)の行

![dpaging_before](https://user-images.githubusercontent.com/73451469/212553958-7e899efc-c885-4b74-bde2-bc42e7e62248.png)

改造後の実行時間(elapsed=...)の行

![dpagin_after](https://user-images.githubusercontent.com/73451469/212553899-333b092d-144b-456a-ac7d-4faa491e5941.png)
