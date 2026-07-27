// Data for the interactive tutorial site: keyboard layout, keycap
// annotations, and scripted tutorial steps.
// Screen-state fields (each step merges over the previous state):
//   text   committed text (no underline)
//   comp   composition segments: [char, 's'(settled/black) | 'p'(pending/blue)]
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

// Control / punctuation hints shown bottom-left in gray.
const CONTROLS = {
  '1':'ˉ', '2':'ˊ', '3':'ˇ', '4':'ˋ', '5':'˙',
  '6':'選', '7':'頁◂', '8':'選單', '9':'◂', '0':'▸',
  ',':'，', '.':'。', '/':'？', '[':'「', ']':'」', "'":'『』', '`':'注音'
};

const TUTORIALS = [
  {
    id: 'intro', title: '總覽與鍵帽標註',
    steps: [
      { keys: [], screen: {},
        cap: '歡迎！下方是可以<b>拖曳轉動</b>的 3D 鍵盤。每個鍵帽上：大字＝按鍵本身、右上<b>橘色＝聲母</b>注音、右下<b>青色＝韻母</b>注音；數字排的灰字是控制功能。每個音節固定「聲母鍵＋韻母鍵」兩鍵，之後可補聲調數字。' },
      { keys: ['u'], screen: { comp: [['ㄕ','p']] },
        cap: '例：<kbd>u</kbd> 的聲母是 ㄕ。第一鍵一律顯示注音（藍色＝音節未完成），不顯示英文字母。' },
      { keys: ['l'], screen: { comp: [['篩','p']] },
        cap: '<kbd>l</kbd> 的韻母是 ㄞ，「shai」兩鍵完成、立即成字。不打聲調時只出一聲＋輕聲，所以先看到「篩」。' },
      { keys: ['4'], screen: { comp: [['曬','s']] },
        cap: '補上 <kbd>4</kbd>（四聲）修正成「曬」，字轉黑＝聲調已定。左側選單有各主題的動畫教學，開始吧！' },
      { keys: ['Enter'], screen: { text: '曬', comp: [] },
        cap: '<kbd>Enter</kbd>（或空白鍵）整段上屏。' }
    ]
  },
  {
    id: 'basics', title: '基礎：兩鍵一音節',
    steps: [
      { keys: ['n'], screen: { comp: [['ㄋ','p']] },
        cap: '目標：打出「你好」。先按聲母鍵 <kbd>n</kbd>＝ㄋ，畫面即時顯示注音。' },
      { keys: ['i'], screen: { comp: [['妮','p']] },
        cap: '再按韻母鍵 <kbd>i</kbd>＝ㄧ，兩鍵一落音節立即成字（見字即所得）。還沒打聲調，暫出一聲的「妮」，藍色＝仍可修改。' },
      { keys: ['3'], screen: { comp: [['你','s']] },
        cap: '<kbd>3</kbd>＝三聲，回填到剛完成的音節：「妮」→「你」，轉黑定調。' },
      { keys: ['h'], screen: { comp: [['你','s'],['ㄏ','p']] },
        cap: '直接接著打下一個音節：<kbd>h</kbd>＝ㄏ。' },
      { keys: ['k'], screen: { comp: [['你','s'],['蒿','p']] },
        cap: '<kbd>k</kbd>＝ㄠ，「hao」完成，暫顯一聲的「蒿」——整句轉換隨時會依上下文修正，不用管它。' },
      { keys: ['3'], screen: { comp: [['你','s'],['好','s']] },
        cap: '補上三聲，整句轉換自動把「你蒿」修正為「<b>你好</b>」。' },
      { keys: ['Space'], screen: { text: '你好', comp: [] },
        cap: '空白鍵＝整段上屏（與 Enter 相同），底線消失。恭喜完成第一句！' }
    ]
  },
  {
    id: 'tones', title: '聲調規則（嚴格模式）',
    steps: [
      { keys: ['w'], screen: { comp: [['ㄨ','p']] },
        cap: '試試打「我」。<kbd>w</kbd>＝ㄨ。' },
      { keys: ['o'], screen: { comp: [['窩','p']] },
        cap: '注意：<b>不打聲調＝只出一聲＋輕聲的字</b>，所以是「窩」——三聲的「我」不會自己跑出來。聲調是語意的一部分。' },
      { keys: ['3'], screen: { comp: [['我','s']] },
        cap: '明打 <kbd>3</kbd> 才有「我」。' },
      { keys: ['Space'], screen: { text: '我', comp: [] },
        cap: '上屏。接著看輕聲的情況。' },
      { keys: ['d'], screen: { text: '我', comp: [['ㄉ','p']] },
        cap: '<kbd>d</kbd>＝ㄉ。' },
      { keys: ['e'], screen: { text: '我', comp: [['的','p']] },
        cap: '<kbd>e</kbd>＝ㄜ。「de」不打數字時，一聲與輕聲都在範圍內，最常用的輕聲「的」直接出現——高頻字不用多按一鍵。' },
      { keys: ['Space'], screen: { text: '我的', comp: [] },
        cap: '整理一下：不打數字＝一聲＋輕聲；<kbd>1</kbd>＝嚴格只出一聲；<kbd>2</kbd><kbd>3</kbd><kbd>4</kbd>＝精確聲調；<kbd>5</kbd>＝只出輕聲。' }
    ]
  },
  {
    id: 'menu', title: '選字：游標與選單',
    steps: [
      { keys: ['n','i','3','h','k','3'], screen: { comp: [['你','s'],['好','s']] },
        cap: '先打出「你好」（ni3hk3）。組字中<b>數字全是控制鍵</b>：<kbd>9</kbd>/<kbd>0</kbd> 移游標、<kbd>8</kbd> 開選單。' },
      { keys: ['9'], screen: { anchor: 1 },
        cap: '<kbd>9</kbd>＝游標左移。反白的「好」＝目前的選字對象（游標右邊那個字）。' },
      { keys: ['9'], screen: { anchor: 0 },
        cap: '再按 <kbd>9</kbd>，反白移到「你」。<kbd>0</kbd> 往右移；到兩端會環繞到另一頭。' },
      { keys: ['8'], screen: { menu: { anchor: 0, page: '1/2', sel: null,
          items: ['妳','擬','旎','苨','柅','狔'] } },
        cap: '<kbd>8</kbd>＝對反白字開候選選單。注意目前顯示的「你」不會列出——選了也不會變的候選一律隱藏。' },
      { keys: ['8'], screen: { menu: { anchor: 0, page: '2/2', sel: null,
          items: ['晲','齯','薿','隬','鑈','抳'] } },
        cap: '選單開著時 <kbd>8</kbd>＝下一頁、<kbd>7</kbd>＝上一頁，翻到底不環繞。' },
      { keys: ['7'], screen: { menu: { anchor: 0, page: '1/2', sel: 0,
          items: ['妳','擬','旎','苨','柅','狔'] } },
        cap: '<kbd>7</kbd> 回到第一頁。每頁最多 6 個候選，對應數字 <kbd>1</kbd>–<kbd>6</kbd>。' },
      { keys: ['1'], screen: { comp: [['妳','s'],['好','s']], anchor: 0, menu: null },
        cap: '<kbd>1</kbd> 選「妳」：選單關閉、該詞段釘選，選擇會被記住（使用者選字學習）。選單開著時按<b>其他任何鍵</b>＝關窗並執行那個鍵原本的功能。' },
      { keys: ['Enter'], screen: { text: '妳好', comp: [], anchor: null },
        cap: '<kbd>Enter</kbd> 上屏「妳好」。' }
    ]
  },
  {
    id: 'shift', title: '中英切換（Shift）',
    steps: [
      { keys: ['v','s'], screen: { comp: [['中','p']] },
        cap: '打「中文」：<kbd>v</kbd>＝ㄓ、<kbd>s</kbd>＝ㄨㄥ →「中」（一聲免打數字）。' },
      { keys: ['w','f','2'], screen: { comp: [['中','s'],['文','s']] },
        cap: '<kbd>w</kbd>＝ㄨ、<kbd>f</kbd>＝ㄣ、<kbd>2</kbd>＝二聲 →「文」。' },
      { keys: ['ShiftL'], screen: { text: '中文 ', comp: [], mode: 'en' },
        cap: '<b>單獨輕按 Shift</b>＝中英切換。組字中按下會先自動上屏，且左邊是中文字時自動補一個半形空白。狀態列變成「英」。' },
      { keys: ['o','k'], screen: { text: '中文 OK', mode: 'en' },
        cap: '英文模式＝按鍵全數放行，直接輸入、沒有底線（畫面示範輸入 OK）。' },
      { keys: ['ShiftL'], screen: { text: '中文 OK ', mode: 'zh' },
        cap: '再輕按 Shift 切回中文：左邊是英文字母，同樣自動補空白。<b>Shift＋字母</b>照常輸出大寫，不會誤觸切換。' },
      { keys: ['h','k','3'], screen: { text: '中文 OK ', comp: [['好','s']] },
        cap: '無縫接回中文。要打數字？中文模式的數字排是控制鍵（閒置時按了無作用），先 Shift 切英文再打，分隔空白它替你管。' }
    ]
  },
  {
    id: 'punct', title: '標點符號',
    steps: [
      { keys: ['n','i','3','h','k','3','Space'], screen: { text: '你好', comp: [] },
        cap: '先上屏「你好」。' },
      { keys: [','], screen: { text: '你好，' },
        cap: '中文模式下 <kbd>,</kbd> 直接上屏全形「，」。' },
      { keys: ['z','l','4','j','m','4'], screen: { text: '你好，', comp: [['再','s'],['見','s']] },
        cap: '接著打「再見」：zai4＝<kbd>z</kbd><kbd>l</kbd><kbd>4</kbd>、jian4＝<kbd>j</kbd><kbd>m</kbd><kbd>4</kbd>。' },
      { keys: ['Space'], screen: { text: '你好，再見', comp: [] },
        cap: '上屏。' },
      { keys: ['.'], screen: { text: '你好，再見。' },
        cap: '<kbd>.</kbd>＝「。」。其他對應：<kbd>?</kbd>？　<kbd>!</kbd>！　<kbd>:</kbd>：　<kbd>\\</kbd>、　<kbd>[</kbd><kbd>]</kbd>「」　<kbd>{</kbd><kbd>}</kbd>『』　<kbd>(</kbd><kbd>)</kbd>（）　<kbd>&lt;</kbd><kbd>&gt;</kbd>《》；引號 <kbd>"</kbd>/<kbd>\'</kbd> 開閉交替。<kbd>;</kbd> 單獨按是「；」，組字中仍是 ing 韻母鍵。' }
    ]
  },
  {
    id: 'bopomofo', title: '進階：打注音符號',
    steps: [
      { keys: ['n'], screen: { comp: [['ㄋ','p']] },
        cap: '有時想輸出注音符號本身。按 <kbd>n</kbd>，出現藍色的 ㄋ。' },
      { keys: ['`'], screen: { comp: [['ㄋ','s']] },
        cap: '反引號 <kbd>`</kbd>＝把待定注音「<b>定案</b>」成固定黑字，融入組字串——可與中文混排、游標編輯、Backspace 逐符號刪。' },
      { keys: ['`','k'], screen: { comp: [['ㄋ','s'],['ㄠ','s']] },
        cap: '沒有待定注音時，<kbd>`</kbd> 會挖空聲母：下一鍵直接讀成<b>韻母</b>並定案。<kbd>`</kbd><kbd>k</kbd>＝ㄠ。' },
      { keys: ['Space'], screen: { text: 'ㄋㄠ', comp: [] },
        cap: '<kbd>Space</kbd> 與整段一起上屏。另外：藍色注音殘鍵直接按 Space 也會以符號上屏（n＋Space→ㄋ）；Enter 則丟棄藍色殘鍵。' }
    ]
  }
];
