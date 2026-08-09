// Data for the interactive tutorial site: keyboard layout, keycap
// annotations, and scripted tutorial steps.
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

// Initial (聲母) shown top-right in orange.
const INITIALS = {
  b:'ㄅ', p:'ㄆ', m:'ㄇ', f:'ㄈ', d:'ㄉ', t:'ㄊ', n:'ㄋ', l:'ㄌ',
  g:'ㄍ', k:'ㄎ', h:'ㄏ', j:'ㄐ', q:'ㄑ', x:'ㄒ', r:'ㄖ',
  z:'ㄗ', c:'ㄘ', s:'ㄙ', v:'ㄓ', i:'ㄔ', u:'ㄕ', y:'ㄧ', w:'ㄨ', o:'零'
};

// Final (韻母) shown bottom-right in teal (primary reading only; see spec).
const FINALS = {
  a:'ㄚ', o:'ㄛ ㄨㄛ', e:'ㄜ', i:'ㄧ', u:'ㄨ', y:'ㄩ ㄨㄞ',
  l:'ㄞ', z:'ㄟ', k:'ㄠ', b:'ㄡ', j:'ㄢ', f:'ㄣ', h:'ㄤ', g:'ㄥ',
  r:'ㄦ ㄨㄢ', w:'ㄧㄚ ㄨㄚ', x:'ㄧㄝ', c:'ㄧㄠ', q:'ㄧㄡ', m:'ㄧㄢ',
  n:'ㄧㄣ', d:'ㄧㄤ ㄨㄤ', ';':'ㄧㄥ', s:'ㄨㄥ', v:'ㄨㄟ', p:'ㄨㄣ', t:'ㄩㄝ'
};

// Control / punctuation hints shown bottom-left in gray. Every digit
// carries a tone mark: 1-5 for the left hand, 0-6 mirrored around the 5/6
// gap for the right. 7/8/9/0 also show the control they perform once the
// syllable is settled (頁=page, 選=menu, ◂▸=cursor).
const CONTROLS = {
  '1':'ˉ', '2':'ˊ', '3':'ˇ', '4':'ˋ', '5':'˙',
  '6':'˙', '7':'ˋ頁', '8':'ˇ選', '9':'ˊ◂', '0':'ˉ▸',
  ',':'，', '.':'。', '/':'？', '[':'「', ']':'」', "'":'『』', '`':'注音',
  'Space':'定案', 'Tab':'⌫'
};

const TUTORIALS = [
  {
    id: 'intro', title: '總覽與鍵帽標註',
    steps: [
      { keys: [], screen: {},
        cap: '歡迎！下方是可以<b>拖曳轉動</b>的 3D 鍵盤。每個鍵帽上：大字＝按鍵本身、右上<b>橘色＝聲母</b>注音、右下<b>青色＝韻母</b>注音；數字排的灰字是聲調與控制功能。每個音節＝「聲母鍵＋韻母鍵」兩鍵，之後可補聲調數字。' },
      { keys: ['u'], screen: { comp: [['ㄕ','p']] },
        cap: '例：<kbd>u</kbd> 的聲母是 ㄕ。第一鍵一律顯示注音，不顯示英文字母；虛線底線代表這段還沒上屏。' },
      { keys: ['l'], screen: { comp: [['ㄕㄞ','p']] },
        cap: '<kbd>l</kbd> 的韻母是 ㄞ，「shai」兩鍵完成一個音節——畫面<b>先維持注音</b>，等你決定聲調。' },
      { keys: ['4'], screen: { comp: [['曬','s']] },
        cap: '補上 <kbd>4</kbd>（四聲）：注音<b>立刻變成字</b>「曬」。想用右手打四聲也可以，<kbd>7</kbd> 和 <kbd>4</kbd> 完全等價。' },
      { keys: ['Enter'], screen: { text: '曬', comp: [] },
        cap: '<kbd>Enter</kbd> 整段上屏。不打聲調的話畫面會停在注音，這時按<b>空白鍵</b>就以「一聲／輕聲」定案成字——見〈空白鍵〉那課。' }
    ]
  },
  {
    id: 'basics', title: '基礎：兩鍵一音節',
    steps: [
      { keys: ['n'], screen: { comp: [['ㄋ','p']] },
        cap: '目標：打出「你好」。先按聲母鍵 <kbd>n</kbd>＝ㄋ，畫面即時顯示注音。' },
      { keys: ['i'], screen: { comp: [['ㄋㄧ','p']] },
        cap: '再按韻母鍵 <kbd>i</kbd>＝ㄧ，兩鍵完成一個音節——畫面維持注音 ㄋㄧ。（內部已經在跑整句轉換了，只是還沒顯示成字。）' },
      { keys: ['3'], screen: { comp: [['你','s']] },
        cap: '<kbd>3</kbd>＝三聲，套到剛完成的音節：<b>字馬上出現</b>——「你」。聲調鍵就是最直接的定案方式。' },
      { keys: ['h'], screen: { comp: [['你','s'],['ㄏ','p']] },
        cap: '直接接著打下一個音節：<kbd>h</kbd>＝ㄏ。' },
      { keys: ['k'], screen: { comp: [['你','s'],['ㄏㄠ','p']] },
        cap: '<kbd>k</kbd>＝ㄠ，「hao」完成，維持注音等聲調。<b>就算不打聲調</b>，下一個音節的首鍵也會把它定案成字，不必特地按空白。' },
      { keys: ['3'], screen: { comp: [['你','s'],['好','s']] },
        cap: '補上三聲 → 「好」。每定案一個音節就重跑整句轉換，前面的字會依上下文自動修正。' },
      { keys: ['Enter'], screen: { text: '你好', comp: [] },
        cap: '<kbd>Enter</kbd> 整段上屏，底線消失。恭喜完成第一句！' }
    ]
  },
  {
    id: 'single', title: '單鍵音節（省一鍵）',
    steps: [
      { keys: [], screen: {},
        cap: '<b>26 個字母鍵每一個都是一個音節</b>，韻母鍵可以省略。一種是注音本身就是音節：<kbd>z</kbd>ㄗ <kbd>c</kbd>ㄘ <kbd>s</kbd>ㄙ <kbd>r</kbd>ㄖ <kbd>v</kbd>ㄓ <kbd>i</kbd>ㄔ <kbd>u</kbd>ㄕ <kbd>y</kbd>ㄧ <kbd>w</kbd>ㄨ <kbd>a</kbd>ㄚ <kbd>e</kbd>ㄜ <kbd>o</kbd>ㄛ。另一種是<b>注音的呼名</b>：ㄅㄆㄇㄈ 帶 ㄛ、ㄉㄊㄋㄌㄍㄎㄏ 帶 ㄜ、ㄐㄑㄒ 帶 ㄧ——所以「的」＝<kbd>d</kbd>＋空白、「了」＝<kbd>l</kbd>＋空白。' },
      { keys: ['w','f','2'], screen: { comp: [['文','s']] },
        cap: '示範打「文字」。先打「文」：<kbd>w</kbd><kbd>f</kbd><kbd>2</kbd>（ㄨㄣˊ）。' },
      { keys: ['z'], screen: { comp: [['文','s'],['ㄗ','p']] },
        cap: '「字」的注音是 ㄗˋ——只要按 <kbd>z</kbd>，畫面就是 ㄗ。' },
      { keys: ['4'], screen: { comp: [['文','s'],['字','s']] },
        cap: '直接補 <kbd>4</kbd>：「字」出來了。<b>一個字只花兩鍵</b>（含聲調）。' },
      { keys: ['Enter'], screen: { text: '文字', comp: [] },
        cap: '<kbd>Enter</kbd> 上屏。再看一聲／輕聲的情況。' },
      { keys: ['v'], screen: { text: '文字', comp: [['ㄓ','p']] },
        cap: '「知道」的「知」＝ㄓ（一聲）。按 <kbd>v</kbd>。' },
      { keys: ['Space'], screen: { text: '文字', comp: [['之','s']] },
        cap: '空白鍵以「一聲／輕聲」定案——先跑出來的是最常用的「之」，別急。' },
      { keys: ['d','k','4'], screen: { text: '文字', comp: [['知','s'],['道','s']] },
        cap: '接著打「道」（<kbd>d</kbd><kbd>k</kbd><kbd>4</kbd>）：整句轉換一跑，前面的「之」<b>自動修正成「知」</b>。' },
      { keys: [], screen: {},
        cap: '注意：這些鍵<b>仍然可以接韻母鍵</b>（<kbd>u</kbd>＋<kbd>l</kbd>＝ㄕㄞ、<kbd>d</kbd>＋<kbd>l</kbd>＝ㄉㄚ），只有聲調鍵和空白鍵會把單鍵當成一個音節收掉。省略韻母鍵＝<b>幫你按下那個預設韻母鍵</b>，所以 <kbd>d</kbd> 和 <kbd>d</kbd><kbd>e</kbd> 是同一個讀音 ㄉㄜ。' },
      { keys: ['v','Space'], screen: { text: '', comp: [['之','s']] },
        cap: '最重要的一點：單鍵音節後面<b>一定要用空白鍵（或聲調鍵）收尾</b>，才接得下一個字。打「知情」先按 <kbd>v</kbd> 再按空白。' },
      { keys: ['q',';','2'], screen: { comp: [['知','s'],['情','s']] },
        cap: '接著打「情」（<kbd>q</kbd><kbd>;</kbd><kbd>2</kbd>）——整句轉換一跑，「之」修正成「知」。' },
      { keys: ['Enter'], screen: { text: '知情', comp: [] },
        cap: '「知識」也一樣：<kbd>v</kbd>＋空白＋<kbd>u</kbd><kbd>4</kbd>。<b>沒有例外</b>——不要試著直接連打 <kbd>v</kbd><kbd>u</kbd>，那兩鍵是 ㄓㄨ（住／主）。' },
      { keys: [], screen: {},
        cap: '<kbd>y</kbd> 維持 ㄧ 而不是 ㄩ：詞庫統計 ㄧ 系音節的使用頻率是 ㄩ 系的 <b>4 倍</b>，所以一／以／意可以兩鍵打完（<kbd>y</kbd>＋空白、<kbd>y</kbd><kbd>3</kbd>、<kbd>y</kbd><kbd>4</kbd>）；ㄩ 仍是 <kbd>y</kbd><kbd>u</kbd> 加聲調。' }
    ]
  },
  {
    id: 'tones', title: '聲調規則（嚴格模式）',
    steps: [
      { keys: ['w'], screen: { comp: [['ㄨ','p']] },
        cap: '試試打「我」。<kbd>w</kbd>＝ㄨ。' },
      { keys: ['o'], screen: { comp: [['ㄨㄛ','p']] },
        cap: '<kbd>o</kbd>＝ㄛ，音節完成，維持注音 ㄨㄛ。' },
      { keys: ['3'], screen: { comp: [['我','s']] },
        cap: '明打 <kbd>3</kbd> 才會有「我」。<b>不打聲調＝只出一聲＋輕聲的字</b>，三聲的「我」不會自己跑出來——聲調是語意的一部分。' },
      { keys: ['Backspace'], screen: { comp: [] },
        cap: '打錯調怎麼辦？<b>按 <kbd>⌫</kbd> 把整個音節刪掉重打</b>（聲調鍵一按就成字，沒有「只退聲調」這一步）。<kbd>Tab</kbd> 與 <kbd>⌫</kbd> 等價，右手不用離開主鍵區。' },
      { keys: ['w','o','3','Enter'], screen: { text: '我', comp: [] },
        cap: '重打 wo3 並 <kbd>Enter</kbd> 上屏。接著看輕聲的情況。' },
      { keys: ['d'], screen: { text: '我', comp: [['ㄉ','p']] },
        cap: '<kbd>d</kbd>＝ㄉ。' },
      { keys: ['e'], screen: { text: '我', comp: [['ㄉㄜ','p']] },
        cap: '<kbd>e</kbd>＝ㄜ。「de」完成，還沒定案。' },
      { keys: ['Space'], screen: { text: '我', comp: [['的','s']] },
        cap: '空白鍵＝用「不打數字」的預設定案：一聲與輕聲都在範圍內，最常用的輕聲「<b>的</b>」直接出現——高頻字不用多按一鍵。' },
      { keys: ['g','e'], screen: { text: '我', comp: [['的','s'],['ㄍㄜ','p']] },
        cap: '再看一個左右手的對照。打 ge：<kbd>g</kbd>＝ㄍ、<kbd>e</kbd>＝ㄜ。' },
      { keys: ['0'], screen: { comp: [['的','s'],['歌','s']] },
        cap: '<b>聲調鍵可以用右手</b>：以 <kbd>5</kbd>/<kbd>6</kbd> 之間為軸鏡像，<kbd>0</kbd>＝一聲、<kbd>9</kbd>＝二聲、<kbd>8</kbd>＝三聲、<kbd>7</kbd>＝四聲、<kbd>6</kbd>＝輕聲。這裡的 <kbd>0</kbd> 與 <kbd>1</kbd> 等價，定案成「歌」——<b>明打一聲</b>與不打數字不一樣：不打數字會出輕聲的「個」。' },
      { keys: ['Space'], screen: { text: '我的歌', comp: [] },
        cap: '沒有待定的音了，這一下空白鍵才上屏。整理：不打數字＝一聲＋輕聲；<kbd>1</kbd>/<kbd>0</kbd>＝嚴格只出一聲；<kbd>2</kbd><kbd>3</kbd><kbd>4</kbd>（或 <kbd>9</kbd><kbd>8</kbd><kbd>7</kbd>）＝精確聲調；<kbd>5</kbd>/<kbd>6</kbd>＝只出輕聲。' }
    ]
  },
  {
    id: 'space', title: '空白鍵：定案與上屏',
    steps: [
      { keys: ['v','s'], screen: { comp: [['ㄓㄨㄥ','p']] },
        cap: '空白鍵不是「上屏鍵」，是「<b>定案鍵</b>」。先打 zhong：<kbd>v</kbd>＝ㄓ、<kbd>s</kbd>＝ㄨㄥ，維持注音。' },
      { keys: ['Space'], screen: { comp: [['中','s']] },
        cap: '空白鍵把它以預設的一聲／輕聲定案成「中」。<b>注意底線還在</b>——字只是定案，沒有上屏。' },
      { keys: ['w','f','2'], screen: { comp: [['中','s'],['文','s']] },
        cap: '接著打「文」（<kbd>w</kbd><kbd>f</kbd><kbd>2</kbd>）。給了聲調就<b>直接成字</b>，不必再按空白。底線仍在，整段還沒上屏。' },
      { keys: ['Space'], screen: { text: '中文', comp: [] },
        cap: '這時已經<b>沒有待定的音</b>，空白鍵才整段上屏（與 <kbd>Enter</kbd> 相同）。' },
      { keys: ['n','c'], screen: { text: '中文', comp: [['ㄋㄧㄠ','p']] },
        cap: '如果這個音節<b>沒有一聲也沒有輕聲</b>呢？<kbd>n</kbd><kbd>c</kbd>＝ㄋㄧㄠ，詞庫裡只有 ㄋㄧㄠˇ／ㄋㄧㄠˋ。' },
      { keys: ['Space'], screen: { text: '中文', comp: [['ㄋ','s'],['ㄧ','s'],['ㄠ','s']] },
        cap: '空白鍵這時把注音<b>本身</b>定案成符號留在組字串裡（不是上屏）。<kbd>f</kbd>＝ㄈㄛ 也一樣，因為 ㄈㄛ 只有二聲（佛）。' },
      { keys: ['Enter'], screen: { text: '中文ㄋㄧㄠ', comp: [] },
        cap: '<kbd>Enter</kbd> 一起上屏。反過來說：<kbd>Enter</kbd> 會<b>丟棄</b>還沒定案的注音，想留就先按空白。' }
    ]
  },
  {
    id: 'menu', title: '選字：游標與選單',
    steps: [
      { keys: ['n','i','3','h','k','3'], screen: { comp: [['你','s'],['好','s']] },
        cap: '先打出「你好」（ni3hk3）。兩個音節都給了聲調＝都已定案，<b>數字鍵這時就是控制鍵</b>：<kbd>9</kbd>/<kbd>0</kbd> 移游標、<kbd>8</kbd> 開選單、<kbd>-</kbd>/<kbd>=</kbd> 跳到頭／尾。' },
      { keys: [], screen: {},
        cap: '（反過來說：畫面上還是<b>注音</b>的時候，所有數字都是聲調鍵——那時按 <kbd>8</kbd> 只會被當成三聲。要先讓它變成字，才用得到控制鍵。）' },
      { keys: ['9'], screen: { anchor: 1, cur: 1 },
        cap: '<kbd>9</kbd>＝游標左移。反白的「好」＝目前的選字對象（游標右邊那個字）。' },
      { keys: ['9'], screen: { anchor: 0, cur: 0 },
        cap: '再按 <kbd>9</kbd>，反白移到「你」。<kbd>0</kbd> 往右移；到兩端會環繞到另一頭。' },
      { keys: ['8'], screen: { menu: { anchor: 0, page: '1/5', sel: null,
          items: ['妳好','妳','擬','昵','旎','薿'] } },
        cap: '<kbd>8</kbd>＝對反白字開候選選單。目前顯示的「你」「你好」不會列出——選了也不會變的候選一律隱藏。候選也可能是<b>詞</b>（妳好）。' },
      { keys: ['8'], screen: { menu: { anchor: 0, page: '2/5', sel: null,
          items: ['禰','抳','檷','祢','䦵','聻'] } },
        cap: '選單開著時 <kbd>8</kbd>＝下一頁、<kbd>7</kbd>＝上一頁，翻到底不環繞。' },
      { keys: ['7'], screen: { menu: { anchor: 0, page: '1/5', sel: 1,
          items: ['妳好','妳','擬','昵','旎','薿'] } },
        cap: '<kbd>7</kbd> 回到第一頁。每頁最多 6 個候選，對應數字 <kbd>1</kbd>–<kbd>6</kbd>。' },
      { keys: ['2'], screen: { comp: [['妳','s'],['好','s']], anchor: 1, cur: 1, menu: null },
        cap: '<kbd>2</kbd> 選「妳」：選單關閉、該詞段釘選。<b>游標自動跳到選定詞段之後</b>，反白換成「好」——選到 2 個字的詞就跳 2 格。' },
      { keys: [], screen: {},
        cap: '改字<b>只會動到你選的那幾個字</b>：其他位置就算重新斷詞會變，也會被釘回原狀（不鏽鋼[悲] 選「鋼杯」時，前面的「鏽」不會變回「秀」）。' },
      { keys: [], screen: {},
        cap: '而且<b>改一次就記住了</b>。記的是「在某個字後面，這個讀音要打成哪個字」——下次同樣的上下文自動出對的字，換個上下文則不受影響；改回去也只要一次。' },
      { keys: ['8'], screen: { menu: { anchor: 1, page: '1/2', sel: null,
          items: ['你好','郝','㚼','㝀','🆗','👌'] } },
        cap: '所以直接再按 <kbd>8</kbd> 就對下一個字開窗了——<b>連按 <kbd>8</kbd> 可以一路往右改完整句</b>，不必動游標鍵。' },
      { keys: ['Enter'], screen: { text: '妳好', comp: [], anchor: null, cur: null, menu: null },
        cap: '這個字不用改，直接 <kbd>Enter</kbd>：選單關閉並整段上屏「妳好」。選單開著時按<b>其他任何鍵</b>＝關窗並執行那個鍵原本的功能。' }
    ]
  },
  {
    id: 'shift', title: '中英切換（Shift）',
    steps: [
      { keys: ['v','s','Space'], screen: { comp: [['中','s']] },
        cap: '打「中文」：<kbd>v</kbd>＝ㄓ、<kbd>s</kbd>＝ㄨㄥ，空白鍵定案成一聲的「中」。' },
      { keys: ['w','f','2'], screen: { comp: [['中','s'],['文','s']] },
        cap: '<kbd>w</kbd>＝ㄨ、<kbd>f</kbd>＝ㄣ、<kbd>2</kbd>＝二聲 → 「文」。整段還在組字串裡（底線）。' },
      { keys: ['ShiftL'], screen: { comp: [['中','s'],['文','s'],[' ','s']], mode: 'en' },
        cap: '<b>單獨輕按 Shift</b>＝中英切換。狀態列變成「英」，但<b>組字串不會上屏</b>——底線還在，只是自動補上一個半形空白（因為左邊是中文字）。' },
      { keys: ['o','k'], screen: { comp: [['中','s'],['文','s'],[' ','s'],['OK','s']], mode: 'en' },
        cap: '英文模式打的字<b>直接長在同一個組字串裡</b>（畫面示範 OK）。空白鍵在這裡就是普通空白，所以片語、句子都打得出來。' },
      { keys: ['ShiftL'], screen: { comp: [['中','s'],['文','s'],[' ','s'],['OK','s'],[' ','s']], mode: 'zh' },
        cap: '再輕按 Shift 切回中文：左邊是英文字母，同樣自動補空白。<b>Shift＋字母</b>照常輸出大寫，不會誤觸切換。' },
      { keys: ['h','k','3'], screen: { comp: [['中','s'],['文','s'],[' ','s'],['OK','s'],[' ','s'],['好','s']] },
        cap: '無縫接回中文，全部還在同一段未上屏的組字串裡——中英夾雜的句子可以一口氣打完再檢查。' },
      { keys: ['Enter'], screen: { text: '中文 OK 好', comp: [] },
        cap: '<kbd>Enter</kbd> 才整段上屏。（<kbd>⌫</kbd>／<kbd>Tab</kbd> 在英文段一樣逐字刪，選字選單也照常對其中的中文開窗。）' },
      { keys: [], screen: {},
        cap: '沒有組字串的時候按 Shift 就只是單純切模式，按鍵全數放行、<b>不會自動補空白</b>——已經上屏的字後面要不要空白，由你自己決定。' },
      { keys: [], screen: {},
        cap: '最後一點：中／英模式是<b>每個應用程式各自記憶</b>的。剛開的程式一律從英文開始；你在某個程式切成中文，切到別的程式不會被帶過去，回到原程式時又是中文——和微軟注音的習慣一致。' }
    ]
  },
  {
    id: 'punct', title: '標點符號（不上屏）',
    steps: [
      { keys: ['n','i','3','h','k'], screen: { comp: [['你','s'],['ㄏㄠ','p']] },
        cap: '打「你好」，這次最後一個音節<b>先不給聲調</b>，停在注音 ㄏㄠ。' },
      { keys: [','], screen: { comp: [['你','s'],['好','s'],['，','s']] },
        cap: '<kbd>,</kbd>＝全形「，」。標點會<b>順便定案</b>前面的音節（ㄏㄠ→好），而且<b>自己也留在組字串裡</b>——底線還在，整段都沒上屏。' },
      { keys: ['z','l','4'], screen: { comp: [['你','s'],['好','s'],['，','s'],['再','s']] },
        cap: '直接接著打「再見」。zai4＝<kbd>z</kbd><kbd>l</kbd><kbd>4</kbd>（四聲也可以用右手的 <kbd>7</kbd>）。' },
      { keys: ['j','m','4'], screen: { comp: [['你','s'],['好','s'],['，','s'],['再','s'],['見','s']] },
        cap: 'jian4＝<kbd>j</kbd><kbd>m</kbd><kbd>4</kbd>。' },
      { keys: ['.'], screen: { comp: [['你','s'],['好','s'],['，','s'],['再','s'],['見','s'],['。','s']] },
        cap: '<kbd>.</kbd>＝「。」，同樣加進組字串。整句「你好，再見。」<b>到現在一個字都還沒上屏</b>。' },
      { keys: ['9','9','9'], screen: { anchor: 3, cur: 3 },
        cap: '這正是重點：整段仍然可以改。<kbd>9</kbd> 連按三下，游標<b>走得過標點</b>，反白停在「再」。' },
      { keys: ['8'], screen: { menu: { anchor: 3, page: '1/1', sel: null,
          items: ['在','載','扗','爯','儎','洅'] } },
        cap: '<kbd>8</kbd> 照樣開選單——標點沒有打斷組字，前後文都還在同一段裡可以選字。' },
      { keys: ['Enter'], screen: { text: '你好，再見。', comp: [], anchor: null, cur: null, menu: null },
        cap: '<kbd>Enter</kbd> 才整段上屏。<b>沒有任何自動上屏的界線</b>：想寫多長就多長，上屏時機完全由你決定（<kbd>Enter</kbd>、沒東西可定案時的空白鍵、或點到別的地方）。' },
      { keys: [], screen: {},
        cap: '其他對應：<kbd>?</kbd>？　<kbd>!</kbd>！　<kbd>:</kbd>：　<kbd>\\</kbd>、　<kbd>[</kbd><kbd>]</kbd>「」　<kbd>{</kbd><kbd>}</kbd>『』　<kbd>(</kbd><kbd>)</kbd>（）　<kbd>&lt;</kbd><kbd>&gt;</kbd>《》；引號 <kbd>"</kbd>/<kbd>\'</kbd> 開閉交替。<kbd>;</kbd> 單獨按是「；」，組字中仍是 ing 韻母鍵。閒置時打標點＝直接開一段新的組字串。' }
    ]
  },
  {
    id: 'keys', title: '不離開主鍵區：刪除與游標',
    steps: [
      { keys: [], screen: {},
        cap: '中文模式下，數字排與 <kbd>Tab</kbd> 被借來做編輯，手不必移到方向鍵區。<b>組字中</b>：<kbd>9</kbd>/<kbd>0</kbd> 移游標、<kbd>-</kbd>/<kbd>=</kbd> 跳頭／尾、<kbd>8</kbd> 開選單、<kbd>Tab</kbd>＝<kbd>⌫</kbd>。' },
      { keys: ['n','i','3','h','k','3'], screen: { comp: [['你','s'],['好','s']] },
        cap: '打「你好」。' },
      { keys: ['Tab'], screen: { comp: [['你','s']] },
        cap: '<kbd>Tab</kbd> 就是 Backspace：刪掉「好」。' },
      { keys: ['Enter'], screen: { text: '你', comp: [] },
        cap: '<kbd>Enter</kbd> 上屏。' },
      { keys: ['Tab'], screen: { text: '', comp: [] },
        cap: '<b>沒有組字串的時候</b>，<kbd>Tab</kbd> 會代送真正的 Backspace——連已經上屏的字也刪得掉。同理閒置時 <kbd>9</kbd>/<kbd>0</kbd>＝←/→、<kbd>-</kbd>/<kbd>=</kbd>＝Home/End。' },
      { keys: [], screen: {},
        cap: '代價是中文模式下 <kbd>Tab</kbd> 不能切換欄位、數字排打不出數字——要用時 <b>Shift 切英文</b>即可（英文模式一切照常）。<kbd>Shift</kbd>+<kbd>Tab</kbd> 不攔，反向切換欄位隨時可用。' }
    ]
  },
  {
    id: 'bopomofo', title: '進階：打注音符號',
    steps: [
      { keys: ['n'], screen: { comp: [['ㄋ','p']] },
        cap: '有時想輸出注音符號本身。按 <kbd>n</kbd>，出現待定的 ㄋ。' },
      { keys: ['`'], screen: { comp: [['ㄋ','s']] },
        cap: '反引號 <kbd>`</kbd>＝把待定注音「<b>定案</b>」成固定符號，融入組字串——可與中文混排、游標編輯、Backspace 逐符號刪。（半個音節按空白鍵也是同樣效果。）' },
      { keys: ['`','k'], screen: { comp: [['ㄋ','s'],['ㄠ','s']] },
        cap: '沒有待定注音時，<kbd>`</kbd> 會挖空聲母：下一鍵直接讀成<b>韻母</b>並定案。<kbd>`</kbd><kbd>k</kbd>＝ㄠ。' },
      { keys: ['Enter'], screen: { text: 'ㄋㄠ', comp: [] },
        cap: '<kbd>Enter</kbd> 與整段一起上屏。注意 <kbd>Enter</kbd> 只丟棄<b>還沒定案</b>的注音，已經定案的符號會照樣上屏。' }
    ]
  }
];
