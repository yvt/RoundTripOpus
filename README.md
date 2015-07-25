RoundTripOpus
=============

RoundTripOpusは入力音声を非可逆音声圧縮形式[Opus](http://opus-codec.org/)でエンコード・デコードする
VST/AudioUnitエフェクトです。

RoundTripOpus is the VST/AudioUnit effect which encodes and decodes the audio input using the
lossy audio codec called [Opus](http://opus-codec.org/).

VST/AudioUnit対応ソフトウェアとともに用いると、音声をOpusで圧縮した際にどの程度劣化するのかを
調べることができます。これ以外にも、エフェクタとして使用できる可能性はありますが、安定性
(1日で書いたのであまりテストしていません)や処理遅延を考えるとあまりお勧めできる使用法ではありません。

本エフェクトはあまりテストされていませんので、ホストアプリケーションがクラッシュしても大丈夫なよう
使用前にプロジェクトを保存するなどして下さい。

ライセンス
----------

GPLv3

ビルド方法
----------

[JUCE](http://www.juce.com/)をダウンロードしてアレして下さい。

以下のライブラリが必要となります。

* [libopus](http://opus-codec.org/downloads/)
* [Secret Rabbit Code](http://www.mega-nerd.com/SRC/) a.k.a. libsamplerate

