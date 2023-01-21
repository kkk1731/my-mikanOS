# my-mikanOS
本リポジトリは、『ゼロからのOS自作入門』（内田公太 (2021)、マイナビ出版）を参考にして作った自作 OS を拡張したものを載せています。
現在進行形で拡張していますがペースは遅め。

オリジナルとの差分はこちら(正確にはオリジナルでは無いですが...）
[diff_original](https://github.com/kkk1731/my-mikanOS/commit/bd641a88ddd68bfc7f88b2a90083a89b1a58a4fb#diff-0d6c189bc4fc1bce5a92f744d27cae3313d2f264c04e202c78941e7c33eade9f)

# 拡張内容
## Hugepage の実装
mikanOS では物理メモリの管理を 4KiB ごとに bitmap で行っています。また、メモリのアロケート・ディアロケートを基本的に 1 ページずつ走査して行っています。demand paging なんかも 4KiB ずつやっていますが、大きいサイズを一度に割り当てて高速化させたかったので Hugepage を実装しました。

最終的に Hugepage を透過的に割り当てることをめざし、メモリ管理の粒度を複数種類にします。Linux のバディアロケータは 4MiB までの粒度で細かく管理していますが、まずは 2MiB の粒度だけ追加しています。

結果の比較として、適当なアプリの実行時間を比較します。Hugepage を実装したものとしていないものを比較しています。（アプリのファイル内容は全く同じ、カーネルだけを変更)
- アプリ内容：ファイルを読み込み、コマンドで渡した引数がそのファイル内に何回出現するか調べる。
- 結果：実行時間がだいたい 1/4 くらいに。


改造前の実行時間(elapsed=...)の行

![dpaging_before](https://user-images.githubusercontent.com/73451469/212553958-7e899efc-c885-4b74-bde2-bc42e7e62248.png)

改造後の実行時間(elapsed=...)の行

![dpagin_after](https://user-images.githubusercontent.com/73451469/212553899-333b092d-144b-456a-ac7d-4faa491e5941.png)

# アクティブタブ切り替え
ウィンドウをクリックするとそのウィンドウがアクティブになり、
- 上部バーが明るくなる
- コマンド入力など操作を受け付ける
という機能が元々あります。
それをコマンド（Alt + t）でもできるようにしました。
