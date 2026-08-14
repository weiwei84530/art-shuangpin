// Data for the interactive tutorial site: keyboard layout, keycap
// annotations, and scripted tutorial steps.
//
// The twelve lessons run in three stages -- 入門 (get a character out),
// 效率 (spend fewer keys), 情境 (everyday situations) -- and each lesson
// uses only what the ones before it have taught.
//
// These steps are hand-written, so scripts\check-tutorials.ps1 replays them
// through the real engine and fails when a screen or a keystroke count no
// longer matches. Two escape hatches, both read by that script:
//   alt: true     the step deliberately shows a longer way round
//   audit: false  the engine cannot model this step (it is TSF-layer
//                 behaviour, like an idle digit injecting a keystroke);
//                 the replay of that lesson stops here
//
// Lesson fields: id, stage, title, steps.
// Screen-state fields (each step merges over the previous state):
//   text   committed text (no underline)
//   comp   composition segments: [text, 's'(settled) | 'p'(pending)]
//          The real IME draws the whole composition in one color with a
//          dotted underline; 'p' segments are the ones still shown as
//          BOPOMOFO (UNSETTLED, i.e. no tone given yet), not a different
//          color.
//   anchor index into comp highlighted as the selection anchor, or null
//   menu   {anchor, items:[...], page:'1/2', sel:index|null} or null
//   mode   'zh' | 'en'

// [key id, cap label, width in u]
const KEY_ROWS = [
  [['`','`',1],['1','1',1],['2','2',1],['3','3',1],['4','4',1],['5','5',1],['6','6',1],
   ['7','7',1],['8','8',1],['9','9',1],['0','0',1],['-','-',1],['=','=',1],['Backspace','⌫',2]],
  [['Tab','Tab',1.5],['q','Q',1],['w','W',1],['e','E',1],['r','R',1],['t','T',1],['y','Y',1],
   ['u','U',1],['i','I',1],['o','O',1],['p','P',1],['[','[',1],[']',']',1],['\\','\\',1.5]],
  [['Caps','Caps',1.75],['a','A',1],['s','S',1],['d','D',1],['f','F',1],['g','G',1],['h','H',1],
   ['j','J',1],['k','K',1],['l','L',1],[';',';',1],["'","'",1],['Enter','Enter',2.25]],
  [['ShiftL','Shift',2.25],['z','Z',1],['x','X',1],['c','C',1],['v','V',1],['b','B',1],['n','N',1],
   ['m','M',1],[',',',',1],['.','.',1],['/','/',1],['ShiftR','Shift',2.75]],
  [['CtrlL','Ctrl',1.25],['Win','Win',1.25],['AltL','Alt',1.25],['Space','',6.25],
   ['AltR','Alt',1.25],['Fn','Fn',1.25],['Menu','☰',1.25],['CtrlR','Ctrl',1.25]]
];

// Initial (聲母) shown top-right in orange. a/e/o carry none: they are
// vowel keys that also open a zero-initial syllable (安 = oj), exactly like
// e in 恩 = ef, so marking only o would be arbitrary.
const INITIALS = {
  b:'ㄅ', p:'ㄆ', m:'ㄇ', f:'ㄈ', d:'ㄉ', t:'ㄊ', n:'ㄋ', l:'ㄌ',
  g:'ㄍ', k:'ㄎ', h:'ㄏ', j:'ㄐ', q:'ㄑ', x:'ㄒ', r:'ㄖ',
  z:'ㄗ', c:'ㄘ', s:'ㄙ', v:'ㄓ', i:'ㄔ', u:'ㄕ', y:'ㄧ', w:'ㄨ'
};

// The vowel an initial is recited with, shown in small type under it: these
// keys are a syllable on their own (ㄎ alone is ㄎㄜ, so 可 = k + tone).
// The other initials already ARE their syllable (ㄗ ㄘ ㄙ ㄖ ㄓ ㄔ ㄕ) and
// need no second glyph. Mirrors DefaultFinalKey() in core/double_pinyin.cpp.
const SINGLE_VOWELS = {
  b:'ㄛ', p:'ㄛ', m:'ㄛ', f:'ㄛ',
  d:'ㄜ', t:'ㄜ', n:'ㄜ', l:'ㄜ', g:'ㄜ', k:'ㄜ', h:'ㄜ',
  j:'ㄧ', q:'ㄧ', x:'ㄧ'
};

// Final (韻母) shown bottom-right in teal: every reading the key produces as
// the SECOND key of a syllable, one per line, always ordered plain → ㄧ →
// ㄨ → ㄩ. The ㄩ readings are the ones that only follow ㄐㄑㄒ (ju = ㄐㄩ,
// jr = ㄐㄩㄢ), so they always sit on the bottom line. Derived from
// FinalKeyMap + ConsonantFinalZhuyin in core/double_pinyin.cpp; after y/w
// the same keys spell the ㄧ/ㄨ series instead, which is a lesson, not a
// keycap.
const FINALS = {
  a:['ㄚ'], o:['ㄛ','ㄨㄛ'], e:['ㄜ'], i:['ㄧ'], u:['ㄨ','ㄩ'],
  l:['ㄞ'], z:['ㄟ'], k:['ㄠ'], b:['ㄡ'], j:['ㄢ'], f:['ㄣ'],
  h:['ㄤ'], g:['ㄥ'], r:['ㄦ','ㄨㄢ','ㄩㄢ'],
  w:['ㄧㄚ','ㄨㄚ'], x:['ㄧㄝ'], c:['ㄧㄠ'], q:['ㄧㄡ'], m:['ㄧㄢ'],
  n:['ㄧㄣ'], d:['ㄧㄤ','ㄨㄤ'], ';':['ㄧㄥ'],
  v:['ㄨㄟ','ㄩㄝ'], p:['ㄨㄣ','ㄩㄣ'], s:['ㄨㄥ','ㄩㄥ'],
  y:['ㄨㄞ','ㄩ'], t:['ㄩㄝ']
};

// Control / punctuation hints shown bottom-left in gray. Every digit
// carries a tone mark: 1-5 for the left hand, 0-6 mirrored around the 5/6
// gap for the right. 7/8/9/0 also show the control they perform once the
// syllable is settled (頁=page, 選=menu, ◂▸=cursor).
const CONTROLS = {
  '1':'ˉ', '2':'ˊ', '3':'ˇ', '4':'ˋ', '5':'˙',
  '6':'˙', '7':'ˋ頁', '8':'ˇ選', '9':'ˊ◂', '0':'ˉ▸',
  ',':'，', '.':'。', '/':'、', '[':'「', ']':'」', "'":'『』', '`':'注音',
  'Space':'定案'
};

const TUTORIALS = [
  {
    id: 'layout', stage: '入門', title: '認識鍵盤：雙拼怎麼拼',
    steps: [
      { keys: [], screen: {  },
        cap: '歡迎！下方是可以<b>拖曳轉動</b>的 3D 鍵盤。鍵帽上有三種標註：右上<b>橘色＝聲母</b>、右下<b>青色＝韻母</b>（一個鍵能拼出好幾個韻母時逐行列出，順序固定 ㄧ 系→ㄨ 系→ㄩ 系）、左下<b>灰色＝聲調與控制功能</b>。' },
      { keys: [], screen: {  },
        cap: '這是<b>雙拼</b>：一個音節固定用兩個鍵拼出來，不像注音要一個一個符號按。鍵位就是<b>微軟雙拼</b>那一套（zh→<kbd>v</kbd>、ch→<kbd>i</kbd>、sh→<kbd>u</kbd>、ing→<kbd>;</kbd>），從微軟雙拼過來的人不用重學。' },
      { keys: ['u'], screen: { comp: [['ㄕ','p']] },
        cap: '第一鍵是<b>聲母</b>。<kbd>u</kbd> 的橘色標註是 ㄕ，按下去畫面就出現 ㄕ——<b>第一鍵一律顯示注音</b>，不會顯示英文字母。' },
      { keys: ['l'], screen: { comp: [['ㄕㄞ','p']] },
        cap: '第二鍵是<b>韻母</b>。<kbd>l</kbd> 的青色標註是 ㄞ，兩鍵湊成 ㄕㄞ，一個音節完成。畫面<b>先停在注音</b>，等你決定聲調。' },
      { keys: ['4'], screen: { comp: [['曬','s']] },
        cap: '補上 <kbd>4</kbd>（四聲），注音<b>立刻變成字</b>：曬。一個字最多三鍵——這就是整套輸入法的節奏。' },
      { keys: ['Enter'], screen: { text: '曬', comp: [] },
        cap: '<kbd>Enter</kbd> 上屏，虛線底線消失。<b>底線在＝還沒送出去</b>，隨時都還能改。' }
    ]
  },
  {
    id: 'basics', stage: '入門', title: '第一句：你好',
    steps: [
      { keys: ['n'], screen: { comp: [['ㄋ','p']] },
        cap: '來打「你好」。<kbd>n</kbd>＝ㄋ。' },
      { keys: ['i'], screen: { comp: [['ㄋㄧ','p']] },
        cap: '<kbd>i</kbd>＝ㄧ。注意：<kbd>i</kbd> 當<b>第一鍵</b>是聲母 ㄔ，當<b>第二鍵</b>是韻母 ㄧ——橘色與青色標的就是這兩種身分。' },
      { keys: ['3'], screen: { comp: [['你','s']] },
        cap: '<kbd>3</kbd>＝三聲，套到剛完成的音節上，<b>字馬上出現</b>：你。' },
      { keys: ['h'], screen: { comp: [['你','s'],['ㄏ','p']] },
        cap: '直接接著打下一個音節，<kbd>h</kbd>＝ㄏ。' },
      { keys: ['k'], screen: { comp: [['你','s'],['ㄏㄠ','p']] },
        cap: '<kbd>k</kbd>＝ㄠ，ㄏㄠ 完成。<b>就算不打聲調</b>，下一個音節的第一鍵也會順手把它定案，不必特地按空白。' },
      { keys: ['3'], screen: { comp: [['你','s'],['好','s']] },
        cap: '補上三聲 →「好」。每定案一個音節就重跑一次整句轉換，前面的字會依上下文自動修正。' },
      { keys: ['Enter'], screen: { text: '你好', comp: [] },
        cap: '<kbd>Enter</kbd> 整段上屏。第一句完成！' },
      { keys: [], screen: {  },
        cap: '整個流程只有三個階段：<b>打注音</b>（畫面是注音）→ <b>定案成字</b>（聲調鍵、空白鍵或下一個音節）→ <b>上屏</b>（Enter）。後面每一課都是這三段的變化。' }
    ]
  },
  {
    id: 'tones', stage: '入門', title: '聲調：嚴格語意與左右手鏡像',
    steps: [
      { keys: ['w','o'], screen: { comp: [['ㄨㄛ','p']] },
        cap: '打「我」：<kbd>w</kbd>＝ㄨ、<kbd>o</kbd>＝ㄛ，ㄨㄛ 完成。' },
      { keys: ['Space'], screen: { comp: [['窩','s']] },
        cap: '先看<b>不打聲調</b>會怎樣。空白鍵用預設定案——跑出來是「窩」不是「我」。' },
      { keys: ['Backspace'], screen: { comp: [] },
        cap: '因為<b>不打數字＝只出一聲和輕聲</b>的字，三聲的「我」不在範圍內。聲調是語意的一部分，不會幫你猜。<kbd>⌫</kbd> 刪掉重打。' },
      { keys: ['w','o','3'], screen: { comp: [['我','s']] },
        cap: '明打 <kbd>3</kbd> 才有「我」。<b>打錯聲調就是 <kbd>⌫</kbd> 刪掉整個音節重打</b>——聲調鍵一按就成字，沒有「只退聲調」這一步。' },
      { keys: ['m','a','8'], screen: { comp: [['我','s'],['馬','s']] },
        cap: '接著打「馬」。ㄇㄚˇ 是三聲，但這次用右手的 <kbd>8</kbd>：以 <kbd>5</kbd>/<kbd>6</kbd> 之間為軸<b>左右鏡像</b>，<kbd>0</kbd>＝一聲、<kbd>9</kbd>＝二聲、<kbd>8</kbd>＝三聲、<kbd>7</kbd>＝四聲、<kbd>6</kbd>＝輕聲。' },
      { keys: ['u','h','4'], screen: { comp: [['我','s'],['馬','s'],['上','s']] },
        cap: '「上」是四聲。這次反過來：ㄕㄤ 的 <kbd>u</kbd><kbd>h</kbd> <b>都在右手</b>，所以補調用<b>左手</b>的 <kbd>4</kbd>。原則是<b>讓聲調鍵落在剛打完音節的另一隻手</b>——看音節的<b>最後一個字母鍵</b>在哪邊就往另一邊按，兩手輪流不打結。' },
      { keys: ['Enter'], screen: { text: '我馬上', comp: [] },
        cap: '<kbd>Enter</kbd> 上屏「我馬上」。' },
      { keys: [], screen: {  },
        cap: '整理：<b>不打數字</b>＝一聲＋輕聲（最常用，能省就省）；<kbd>1</kbd>/<kbd>0</kbd>＝嚴格只要一聲；<kbd>2</kbd><kbd>3</kbd><kbd>4</kbd>（或 <kbd>9</kbd><kbd>8</kbd><kbd>7</kbd>）＝精確聲調；<kbd>5</kbd>/<kbd>6</kbd>＝只要輕聲。<b>實務上幾乎不用打一聲和輕聲</b>——不打數字本來就涵蓋這兩個，明打只在你要<b>排除另一個</b>時才有意義。' }
    ]
  },
  {
    id: 'settle', stage: '入門', title: '定案與上屏：空白鍵的兩種工作',
    steps: [
      { keys: ['v','s'], screen: { comp: [['ㄓㄨㄥ','p']] },
        cap: '空白鍵不是「上屏鍵」，是「<b>定案鍵</b>」。先打 ㄓㄨㄥ：<kbd>v</kbd>＝ㄓ、<kbd>s</kbd>＝ㄨㄥ。' },
      { keys: ['Space'], screen: { comp: [['中','s']] },
        cap: '空白鍵以預設的一聲／輕聲定案成「中」。<b>注意底線還在</b>——字只是定案，沒有送出去。' },
      { keys: ['w','f','2'], screen: { comp: [['中','s'],['文','s']] },
        cap: '接著打「文」（<kbd>w</kbd><kbd>f</kbd><kbd>2</kbd>）。給了聲調就直接成字，不用再按空白。整段仍在組字串裡。' },
      { keys: ['Space'], screen: { text: '中文', comp: [] },
        cap: '這時<b>已經沒有待定的音</b>，空白鍵才整段上屏（與 <kbd>Enter</kbd> 相同）。所以空白鍵的規則只有一條：<b>有東西可定案就定案，沒有才上屏</b>。' },
      { keys: [], screen: {  },
        cap: '上屏的時機一共只有三種：<kbd>Enter</kbd>、沒東西可定案時的空白鍵、以及點到別的地方（失焦）。<b>沒有任何自動上屏的界線</b>——想寫多長就多長，全段都還能回頭改。' },
      { keys: [], screen: {  },
        cap: '反過來，<kbd>Enter</kbd> 會<b>丟棄</b>還沒定案的半個音節（例如只按了 <kbd>n</kbd> 的 ㄋ）。想留住它就先按空白鍵定案，見〈打注音符號本身〉那一課。' }
    ]
  },
  {
    id: 'single', stage: '效率', title: '單鍵音節：把兩鍵縮成一鍵',
    steps: [
      { keys: [], screen: {  },
        cap: '<b>26 個字母鍵，每一個單獨按都是一個完整音節</b>，韻母鍵可以整個省略。分兩類。第一類是<b>注音本身就是音節</b>：<kbd>z</kbd>ㄗ <kbd>c</kbd>ㄘ <kbd>s</kbd>ㄙ <kbd>r</kbd>ㄖ <kbd>v</kbd>ㄓ <kbd>i</kbd>ㄔ <kbd>u</kbd>ㄕ <kbd>y</kbd>ㄧ <kbd>w</kbd>ㄨ <kbd>a</kbd>ㄚ <kbd>e</kbd>ㄜ <kbd>o</kbd>ㄛ。' },
      { keys: [], screen: {  },
        cap: '第二類是<b>注音的呼名</b>——你唸「ㄅ」的時候其實唸的是「ㄅㄛ」。所以 ㄅㄆㄇㄈ 帶 ㄛ、ㄉㄊㄋㄌㄍㄎㄏ 帶 ㄜ、ㄐㄑㄒ 帶 ㄧ。鍵盤上<b>橘色聲母下方的小字</b>就是這個預設韻母。' },
      { keys: ['w','o','3'], screen: { comp: [['我','s']] },
        cap: '實際用用看。先打「我」。' },
      { keys: ['d'], screen: { comp: [['我','s'],['ㄉ','p']] },
        cap: '「的」的注音是 ㄉㄜ˙——<kbd>d</kbd> 的呼名正好就是 ㄉㄜ，所以只要按 <kbd>d</kbd>。' },
      { keys: ['Space'], screen: { comp: [['我','s'],['的','s']] },
        cap: '空白鍵收尾，「的」出來了。<b>兩鍵一個字</b>（<kbd>d</kbd> 加空白），而不是三鍵。「了」＝<kbd>l</kbd>＋空白也一樣。' },
      { keys: ['Enter'], screen: { text: '我的', comp: [] },
        cap: '<kbd>Enter</kbd> 上屏。省略韻母鍵的實際做法是<b>幫你按下那個預設韻母鍵</b>，所以 <kbd>d</kbd> 和 <kbd>d</kbd><kbd>e</kbd> 是同一個讀音 ㄉㄜ，<b>不可能有歧義</b>。' },
      { keys: ['w','f','2'], screen: { comp: [['文','s']] },
        cap: '再看一組：打「文字」。「文」＝<kbd>w</kbd><kbd>f</kbd><kbd>2</kbd>。' },
      { keys: ['z'], screen: { comp: [['文','s'],['ㄗ','p']] },
        cap: '「字」是 ㄗˋ，而 <kbd>z</kbd> 本身就是 ㄗ。' },
      { keys: ['4'], screen: { comp: [['文','s'],['字','s']] },
        cap: '補 <kbd>4</kbd>——<b>一個字兩鍵搞定</b>，聲調也含在裡面了。' },
      { keys: ['Enter'], screen: { text: '我的文字', comp: [] },
        cap: '<kbd>Enter</kbd> 上屏。最後是<b>唯一要記的規則</b>。' },
      { keys: ['v'], screen: { comp: [['ㄓ','p']] },
        cap: '單鍵音節<b>後面一定要用空白鍵或聲調鍵收尾</b>，不然下一鍵會被當成它的韻母。打「知識」：先按 <kbd>v</kbd>＝ㄓ。' },
      { keys: ['Space'], screen: { comp: [['之','s']] },
        cap: '空白鍵收尾。跑出來是最常用的「之」——別急，整句轉換還沒看到後面。' },
      { keys: ['u','4'], screen: { comp: [['知','s'],['識','s']] },
        cap: '接著打「識」（<kbd>u</kbd><kbd>4</kbd>），整句轉換一跑，前面的「之」<b>自動修正成「知」</b>。<b>不要直接連打 <kbd>v</kbd><kbd>u</kbd></b>——那兩鍵是 ㄓㄨ（住／主）。' },
      { keys: ['Enter'], screen: { text: '我的文字知識', comp: [] },
        cap: '這些鍵<b>仍然可以接韻母鍵</b>（<kbd>u</kbd>＋<kbd>l</kbd>＝ㄕㄞ、<kbd>d</kbd>＋<kbd>l</kbd>＝ㄉㄞ），只有空白鍵和聲調鍵會把單鍵當成一個完整音節收掉。' },
      { keys: [], screen: {  },
        cap: '一個取捨：<kbd>y</kbd> 是 ㄧ 不是 ㄩ，ㄩ 仍要打 <kbd>y</kbd><kbd>u</kbd>。詞庫統計 ㄧ 系比 ㄩ 系常用（單音節約 1.6 倍、整個系列約 2.3 倍），而詞格的一個節點只能對應一個讀音，兩者無法共用同一鍵。' }
    ]
  },
  {
    id: 'finals', stage: '效率', title: '韻母鍵：三呼與零聲母',
    steps: [
      { keys: [], screen: {  },
        cap: '韻母鍵在<b>第二個位置</b>，鍵帽右下角青色的就是。同一個鍵常常有好幾個讀音（<kbd>d</kbd>＝ㄧㄤ／ㄨㄤ、<kbd>r</kbd>＝ㄦ／ㄨㄢ／ㄩㄢ），<b>選哪一個由聲母決定，不用你判斷</b>：ㄍㄎㄏ 接不了 ㄧ 介音，ㄐㄑㄒ 接不了 ㄨ，剩下的那個就是答案。' },
      { keys: ['j','u'], screen: { comp: [['ㄐㄩ','p']] },
        cap: '最容易漏掉的是 <b>ㄐㄑㄒ 專屬的 ㄩ 系</b>。<kbd>j</kbd>＋<kbd>u</kbd>：<kbd>u</kbd> 在別的聲母後面是 ㄨ，在 ㄐ 後面是 <b>ㄩ</b>。' },
      { keys: ['Space'], screen: { comp: [['居','s']] },
        cap: '空白鍵定案→「居」。同一組還有 <kbd>r</kbd>＝ㄩㄢ（捐 <kbd>j</kbd><kbd>r</kbd>）、<kbd>p</kbd>＝ㄩㄣ（軍 <kbd>j</kbd><kbd>p</kbd>）、<kbd>s</kbd>＝ㄩㄥ（兄 <kbd>x</kbd><kbd>s</kbd>）、<kbd>t</kbd> 或 <kbd>v</kbd>＝ㄩㄝ（決 <kbd>j</kbd><kbd>t</kbd>）。' },
      { keys: ['Backspace'], screen: { comp: [] },
        cap: '刪掉，換個實例。' },
      { keys: ['a','j'], screen: { comp: [['ㄢ','p']] },
        cap: '再看<b>零聲母</b>——ㄢ ㄣ ㄤ ㄥ ㄦ ㄞ ㄟ ㄠ ㄡ 這些自己就能成字的音。通用寫法是 <kbd>o</kbd>＋韻母鍵；開頭是 ㄚ／ㄜ 的還可以<b>把第一個字母打兩次</b>：安＝<kbd>a</kbd><kbd>j</kbd>（也可以 <kbd>o</kbd><kbd>j</kbd>）。' },
      { keys: ['q','r','2'], screen: { comp: [['安','s'],['全','s']] },
        cap: '接著打「全」＝<kbd>q</kbd><kbd>r</kbd><kbd>2</kbd>（ㄑㄩㄢˊ，又是一個 ㄩ 系）。「安全」兩個字五鍵。' },
      { keys: ['Enter'], screen: { text: '安全', comp: [] },
        cap: '<kbd>Enter</kbd> 上屏。其他零聲母：恩＝<kbd>e</kbd><kbd>f</kbd>、二＝<kbd>e</kbd><kbd>r</kbd><kbd>4</kbd>、歐＝<kbd>o</kbd><kbd>b</kbd>、愛＝<kbd>a</kbd><kbd>l</kbd><kbd>4</kbd>。' },
      { keys: [], screen: {  },
        cap: '最後一件事：<b>拼不出音節的鍵組會直接被吃掉</b>，畫面不動。ㄎㄟ、ㄉㄣ、ㄖㄚ 這些國語裡沒有的音，按了不會產生假注音——所以打錯第二鍵時通常當場就看得出來。' }
    ]
  },
  {
    id: 'menu', stage: '效率', title: '選字：游標與候選選單',
    steps: [
      { keys: ['n','i','3','h','k','3'], screen: { comp: [['你','s'],['好','s']] },
        cap: '先打出「你好」。兩個音節都給了聲調，也就是<b>都已經定案</b>了。' },
      { keys: [], screen: {  },
        cap: '這時<b>數字鍵換了一種身分</b>。判斷法很簡單：<b>畫面上還是注音</b>→數字全是聲調鍵；<b>畫面上已經是字</b>→數字是控制鍵。<kbd>9</kbd>/<kbd>0</kbd> 移游標、<kbd>8</kbd> 開選單。' },
      { keys: ['9'], screen: { anchor: 1, cur: 1 },
        cap: '<kbd>9</kbd>＝游標左移。<b>反白的字就是選字對象</b>（游標右邊那個）。' },
      { keys: ['9'], screen: { anchor: 0, cur: 0 },
        cap: '再按一次，反白移到「你」。<kbd>0</kbd> 是往右移，到兩端會環繞到另一頭。' },
      { keys: ['8'], screen: { menu: { anchor: 0, page: '1/5', sel: null,
          items: ['妳好','妳','擬','昵','旎','薿'] } },
        cap: '<kbd>8</kbd> 對反白的字開候選選單。目前顯示的「你」和「你好」<b>不會列出來</b>——選了也不會變的候選一律隱藏。候選也可能是整個<b>詞</b>（妳好）。' },
      { keys: ['8'], screen: { menu: { anchor: 0, page: '2/5', sel: null,
          items: ['禰','抳','檷','祢','䦵','聻'] } },
        cap: '選單開著的時候 <kbd>8</kbd>＝下一頁、<kbd>7</kbd>＝上一頁，<b>翻到底不環繞</b>。' },
      { keys: ['7'], screen: { menu: { anchor: 0, page: '1/5', sel: null,
          items: ['妳好','妳','擬','昵','旎','薿'] } },
        cap: '<kbd>7</kbd> 回到第一頁。每頁最多 6 個，對應 <kbd>1</kbd>–<kbd>6</kbd>。' },
      { keys: ['2'], screen: { comp: [['妳','s'],['好','s']], anchor: 1, cur: 1, menu: null },
        cap: '<kbd>2</kbd> 選「妳」。選單關閉，而且<b>游標自動跳到選定詞段之後</b>，反白換成「好」——選到兩個字的詞就跳兩格。' },
      { keys: ['8'], screen: { menu: { anchor: 1, page: '1/2', sel: null,
          items: ['你好','郝','㚼','㝀','🆗','👌'] } },
        cap: '所以可以<b>連按 <kbd>8</kbd> 一路往右改完整句</b>，中間完全不用碰游標鍵。' },
      { keys: ['Enter'], screen: { text: '妳好', comp: [], anchor: null, cur: null, menu: null },
        cap: '這個字不用改，直接 <kbd>Enter</kbd>：選單關閉並整段上屏。選單開著時按<b>其他任何鍵</b>都是「關窗並執行那個鍵原本的功能」。' },
      { keys: [], screen: {  },
        cap: '還有一個保護：改字<b>只會動到你選的那幾個字</b>。整句重新斷詞時，選定範圍以外被改掉的位置一律釘回原狀，不會出現「改了後面、前面卻跟著變」的狀況。' }
    ]
  },
  {
    id: 'memory', stage: '效率', title: '選字記憶：改一次就記住',
    steps: [
      { keys: ['n','i','3','h','k','3'], screen: { comp: [['你','s'],['好','s']] },
        cap: '接著上一課。一樣打「你好」。' },
      { keys: ['9','9','8'], screen: { anchor: 0, cur: 0, menu: { anchor: 0, page: '1/5', sel: null,
          items: ['妳好','妳','擬','昵','旎','薿'] } },
        cap: '一樣把游標移到「你」並開選單。' },
      { keys: ['2'], screen: { comp: [['妳','s'],['好','s']], anchor: 1, cur: 1, menu: null },
        cap: '一樣選「妳」。' },
      { keys: ['Enter'], screen: { text: '妳好', comp: [], anchor: null, cur: null },
        cap: '<kbd>Enter</kbd> 上屏。到這裡都和上一課相同——重點在下一步。' },
      { keys: ['n','i','3'], screen: { comp: [['妳','s']] },
        cap: '再打一次同樣的 <kbd>n</kbd><kbd>i</kbd><kbd>3</kbd>。<b>這次直接就是「妳」</b>：剛才那一次選擇已經記住了，不用再開選單。' },
      { keys: ['h','k','3'], screen: { comp: [['妳','s'],['好','s']] },
        cap: '後面照常打。<b>改一次就生效</b>，不需要重複好幾次去「養」它。' },
      { keys: ['Enter'], screen: { text: '妳好妳好', comp: [] },
        cap: '<kbd>Enter</kbd> 上屏。' },
      { keys: [], screen: {  },
        cap: '記的內容是「<b>在某個字後面，這個讀音要打成哪個字</b>」——所以只有相同上下文才會套用，換個句子不受影響。<b>沒有時間衰減</b>：最近選的一律排第一，想改回去也只要選一次，一樣便宜。' },
      { keys: [], screen: {  },
        cap: '自動套用時同樣<b>只動那一個位置</b>，句子其他地方不會被連帶改掉；而且<b>你當下手動選的字永遠優先</b>，記憶不會回頭跟你的選擇吵架。記錄存在 <kbd>%APPDATA%\\MspyIME\\user-choices.txt</kbd>。' }
    ]
  },
  {
    id: 'punct', stage: '情境', title: '標點：留在組字串裡，不上屏',
    steps: [
      { keys: ['n','i','3','h','k','3'], screen: { comp: [['你','s'],['好','s']] },
        cap: '打「你好」。' },
      { keys: [','], screen: { comp: [['你','s'],['好','s'],['，','s']] },
        cap: '<kbd>,</kbd>＝全形「，」。標點<b>自己也留在組字串裡</b>——底線還在，整段都沒上屏。（如果前面還有沒定案的注音，標點會順手用預設的一聲／輕聲把它定案掉，所以習慣上先給聲調再打標點。）' },
      { keys: ['z','l','4'], screen: { comp: [['你','s'],['好','s'],['，','s'],['在','s']] },
        cap: '直接接著打「再見」。「再」＝<kbd>z</kbd><kbd>l</kbd><kbd>4</kbd>。' },
      { keys: ['j','m','4'], screen: { comp: [['你','s'],['好','s'],['，','s'],['再','s'],['見','s']] },
        cap: '「見」＝<kbd>j</kbd><kbd>m</kbd><kbd>4</kbd>。' },
      { keys: ['.'], screen: { comp: [['你','s'],['好','s'],['，','s'],['再','s'],['見','s'],['。','s']] },
        cap: '<kbd>.</kbd>＝「。」。整句「你好，再見。」<b>到現在一個字都還沒上屏</b>。' },
      { keys: ['9','9','9'], screen: { anchor: 3, cur: 3 },
        cap: '這正是重點：整段仍然可以改。<kbd>9</kbd> 連按三下，游標<b>走得過標點</b>，反白停在「再」。' },
      { keys: ['8'], screen: { menu: { anchor: 3, page: '1/1', sel: null,
          items: ['在','載','扗','爯','儎','洅'] } },
        cap: '<kbd>8</kbd> 照樣開選單——標點沒有打斷組字，前後文都還在同一段裡。' },
      { keys: ['Enter'], screen: { text: '你好，再見。', comp: [], anchor: null, cur: null, menu: null },
        cap: '<kbd>Enter</kbd> 才整段上屏。閒置時打標點＝<b>直接開一段新的組字串</b>，一樣不會馬上送出去。' },
      { keys: [], screen: {  },
        cap: '對應表：<kbd>,</kbd>，　<kbd>.</kbd>。　<kbd>?</kbd>？　<kbd>!</kbd>！　<kbd>:</kbd>：　<kbd>/</kbd> 或 <kbd>\\</kbd>、　<kbd>[</kbd><kbd>]</kbd>「」　<kbd>{</kbd><kbd>}</kbd>『』　<kbd>(</kbd><kbd>)</kbd>（）　<kbd>&lt;</kbd><kbd>&gt;</kbd>《》　<kbd>^</kbd>……　<kbd>_</kbd>——　<kbd>~</kbd>～；引號 <kbd>"</kbd> 與 <kbd>\'</kbd> 開閉交替。<kbd>;</kbd> 單獨按是「；」，組字中仍是 ㄧㄥ 韻母鍵。' }
    ]
  },
  {
    id: 'shift', stage: '情境', title: '中英切換：Shift 輕按',
    steps: [
      { keys: ['m',';','2'], screen: { comp: [['明','s']] },
        cap: '來打「明天 3 點開會」，這句同時用到中文、數字和自動空白。「明」＝<kbd>m</kbd><kbd>;</kbd><kbd>2</kbd>。' },
      { keys: ['t','m'], screen: { comp: [['明','s'],['ㄊㄧㄢ','p']] },
        cap: '「天」＝<kbd>t</kbd><kbd>m</kbd>，一聲不用打數字。' },
      { keys: ['ShiftL'], screen: { comp: [['明','s'],['天','s'],[' ','s']], mode: 'en' },
        cap: '中文模式下<b>數字排打不出數字</b>（它們被借去當聲調鍵和控制鍵了），所以要<b>單獨輕按 Shift</b> 切到英文。狀態列變「英」，但<b>組字串不會上屏</b>——底線還在，而且因為左邊是中文字，<b>自動補了一個半形空白</b>。' },
      { keys: ['3'], screen: { comp: [['明','s'],['天','s'],[' ','s'],['3','s']] },
        cap: '英文模式打的字<b>直接長在同一個組字串裡</b>。空白鍵在這裡就是普通空白，所以整句英文、片語都打得出來。' },
      { keys: ['ShiftL'], screen: { comp: [['明','s'],['天','s'],[' ','s'],['3','s'],[' ','s']], mode: 'zh' },
        cap: '再輕按一次切回中文。左邊是數字，同樣自動補一個空白。<b>Shift＋字母</b>照常輸出大寫，不會誤觸切換。' },
      { keys: ['d','m','3'], screen: { comp: [['明','s'],['天','s'],[' ','s'],['3','s'],[' ','s'],['點','s']] },
        cap: '無縫接回中文：「點」＝<kbd>d</kbd><kbd>m</kbd><kbd>3</kbd>。' },
      { keys: ['k','l'], screen: { comp: [['明','s'],['天','s'],[' ','s'],['3','s'],[' ','s'],['點','s'],['ㄎㄞ','p']] },
        cap: '「開」＝<kbd>k</kbd><kbd>l</kbd>（一聲）。' },
      { keys: ['h','v','4'], screen: { comp: [['明','s'],['天','s'],[' ','s'],['3','s'],[' ','s'],['點','s'],['開','s'],['會','s']] },
        cap: '「會」＝<kbd>h</kbd><kbd>v</kbd><kbd>4</kbd>。中英夾雜的一整句都還在同一段未上屏的組字串裡。' },
      { keys: ['Enter'], screen: { text: '明天 3 點開會', comp: [] },
        cap: '<kbd>Enter</kbd> 才整段上屏。<kbd>⌫</kbd> 在英文段一樣逐字刪，選字選單也照常對其中的中文開窗。' },
      { keys: [], screen: {  },
        cap: '<b>沒有組字串</b>的時候按 Shift 就只是單純切模式：按鍵全數放行，<b>不會自動補空白</b>——已經上屏的字後面要不要空白，由你自己決定。' },
      { keys: [], screen: {  },
        cap: '中／英模式是<b>每個應用程式各自記憶</b>的。剛開的程式一律從<b>英文</b>開始；你在某個程式切成中文，切到別的程式不會被帶過去，回來時又是中文——和微軟注音的習慣一致。' }
    ]
  },
  {
    id: 'homerow', stage: '情境', title: '不離開主鍵區：刪除與游標',
    steps: [
      { keys: [], screen: {  },
        cap: '這套輸入法把數字排借來做編輯，手不用移到方向鍵區。數字排有<b>兩種身分</b>，分界是「畫面上還有沒有組字串」。' },
      { keys: ['n','i','3','h','k','3'], screen: { comp: [['你','s'],['好','s']] },
        cap: '先打「你好」。<b>組字中</b>：<kbd>9</kbd>/<kbd>0</kbd> 移游標、<kbd>8</kbd> 開選單、<kbd>5</kbd>/<kbd>6</kbd> 往右／往左刪（音節已定案時；畫面還是注音時數字仍是聲調鍵）。' },
      { keys: ['Backspace'], screen: { comp: [['你','s']] },
        cap: '<kbd>⌫</kbd> 刪掉「好」。（音節打到一半時是<b>逐鍵</b>刪，已經成字則是<b>逐字</b>刪。）' },
      { keys: ['Enter'], screen: { text: '你', comp: [] },
        cap: '<kbd>Enter</kbd> 上屏「你」。組字串沒了，數字排立刻換上另一個身分。' },
      { keys: [], screen: { text: '你' }, audit: false,
        cap: '<b>沒有組字串的時候</b>，整排數字都是編輯鍵：<kbd>1</kbd> 行首、<kbd>2</kbd>/<kbd>3</kbd> 選取到行首／行尾、<kbd>4</kbd> 行尾、<kbd>5</kbd> Delete、<kbd>6</kbd> Backspace、<kbd>7</kbd>/<kbd>8</kbd> ↑／↓、<kbd>9</kbd>/<kbd>0</kbd> ←／→。<b>由中間往外讀</b>：左兩個往左、右兩個往右，靠內的那個帶選取。' },
      { keys: [], screen: { text: '你' }, audit: false,
        cap: '這排編輯鍵<b>中英文模式都一樣</b>，習慣不用切換。<kbd>Tab</kbd>、<kbd>-</kbd>、<kbd>=</kbd> 完全沒有被攔截，切換欄位照常。' },
      { keys: [], screen: {  },
        cap: '代價是中文模式下數字排打不出數字。要打就用<b>鍵盤右邊的數字鍵</b>（完全不受影響），或<b>按 Shift 切英文</b>——英文模式下只要已經在打一串字，數字鍵就是數字，<code>user123</code> 一路打完不用切。' }
    ]
  },
  {
    id: 'bopomofo', stage: '情境', title: '打注音符號本身',
    steps: [
      { keys: ['n'], screen: { comp: [['ㄋ','p']] },
        cap: '有時候要輸出注音符號本身（標讀音、教小朋友）。先按 <kbd>n</kbd>，出現待定的 ㄋ。' },
      { keys: ['`'], screen: { comp: [['ㄋ','s']] },
        cap: '反引號 <kbd>`</kbd>＝把待定的注音<b>定案成固定符號</b>，融入組字串——可以和中文混排、用游標編輯、<kbd>⌫</kbd> 逐符號刪。' },
      { keys: ['`','k'], screen: { comp: [['ㄋ','s'],['ㄠ','s']] },
        cap: '<b>沒有</b>待定注音時，<kbd>`</kbd> 會把聲母的位置挖空，<b>下一鍵直接讀成韻母</b>並定案。<kbd>`</kbd><kbd>k</kbd>＝ㄠ。整組 ㄋㄧㄠ 就是 <kbd>n</kbd><kbd>`</kbd><kbd>y</kbd><kbd>`</kbd><kbd>k</kbd>。' },
      { keys: ['Enter'], screen: { text: 'ㄋㄠ', comp: [] },
        cap: '<kbd>Enter</kbd> 與整段一起上屏。' },
      { keys: ['n','c'], screen: { comp: [['ㄋㄧㄠ','p']] },
        cap: '還有一種情況會自動變成注音符號。<kbd>n</kbd><kbd>c</kbd>＝ㄋㄧㄠ，而詞庫裡<b>只有 ㄋㄧㄠˇ 和 ㄋㄧㄠˋ</b>，沒有一聲也沒有輕聲。' },
      { keys: ['Space'], screen: { comp: [['ㄋ','s'],['ㄧ','s'],['ㄠ','s']] },
        cap: '這時空白鍵沒有字可以定案，就把<b>注音本身</b>定案成符號留在組字串裡。<kbd>f</kbd>（ㄈㄛ）也一樣——ㄈㄛ 只有二聲的「佛」，所以 <kbd>f</kbd>＋空白會得到 ㄈ，要「佛」請打 <kbd>f</kbd><kbd>2</kbd>。' },
      { keys: ['Enter'], screen: { text: 'ㄋㄠㄋㄧㄠ', comp: [] },
        cap: '<kbd>Enter</kbd> 上屏。注意 <kbd>Enter</kbd> 只丟棄<b>還沒定案</b>的注音，已經定案的符號會照樣送出去。' }
    ]
  }
];
