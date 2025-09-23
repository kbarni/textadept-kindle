#include <cdk_int.h>

/*
 * $Author: tom $
 * $Date: 2016/11/20 19:04:57 $
 * $Revision: 1.224 $
 */

/*
 * Declare file local prototypes.
 */
static void CDKEntryCallBack (CDKENTRY *entry, chtype character);
static void drawCDKEntryField (CDKENTRY *entry);

DeclareCDKObjects (ENTRY, Entry, setCdk, String);

/*
 * This creates a pointer to an entry widget.
 */
CDKENTRY *newCDKEntry (CDKSCREEN *cdkscreen,
		       int xplace,
		       int yplace,
		       const char *title,
		       const char *label,
		       chtype fieldAttr,
		       chtype filler,
		       EDisplayType dispType,
		       int fWidth,
		       int min,
		       int max,
		       boolean Box,
		       boolean shadow)
{
   /* *INDENT-EQLS* */
   CDKENTRY *entry      = 0;
   int parentWidth      = getmaxx (cdkscreen->window);
   int parentHeight     = getmaxy (cdkscreen->window);
   int fieldWidth       = fWidth;
   int boxWidth         = 0;
   int boxHeight;
   int xpos             = xplace;
   int ypos             = yplace;
   int junk             = 0;
   int horizontalAdjust, oldWidth;

   if ((entry = newCDKObject (CDKENTRY, &my_funcs)) == 0)
        return (0);

   setCDKEntryBox (entry, Box);
   boxHeight = (BorderOf (entry) * 2) + 1;

   /*
    * If the fieldWidth is a negative value, the fieldWidth will
    * be COLS-fieldWidth, otherwise, the fieldWidth will be the
    * given width.
    */
   fieldWidth = setWidgetDimension (parentWidth, fieldWidth, 0);
   boxWidth = fieldWidth + 2 * BorderOf (entry);

   /* Set some basic values of the entry field. */
   entry->label = 0;
   entry->labelLen = 0;
   entry->labelWin = 0;

   /* Translate the label char *pointer to a chtype pointer. */
   if (label != 0)
   {
      entry->label = char2Chtype (label, &entry->labelLen, &junk);
      boxWidth += entry->labelLen;
   }

   oldWidth = boxWidth;
   boxWidth = setCdkTitle (ObjOf (entry), title, boxWidth);
   horizontalAdjust = (boxWidth - oldWidth) / 2;

   boxHeight += TitleLinesOf (entry);

   /*
    * Make sure we didn't extend beyond the dimensions of the window.
    */
   boxWidth = MINIMUM (boxWidth, parentWidth);
   boxHeight = MINIMUM (boxHeight, parentHeight);
   fieldWidth = MINIMUM (fieldWidth,
			 boxWidth - entry->labelLen - 2 * BorderOf (entry));

   /* Rejustify the x and y positions if we need to. */
   alignxy (cdkscreen->window, &xpos, &ypos, boxWidth, boxHeight);

   /* Make the label window. */
   entry->win = newwin (boxHeight, boxWidth, ypos, xpos);
   if (entry->win == 0)
   {
      destroyCDKObject (entry);
      return (0);
   }
   keypad (entry->win, TRUE);

   /* Make the field window. */
   entry->fieldWin = subwin (entry->win, 1, fieldWidth,
			     (ypos + TitleLinesOf (entry) + BorderOf (entry)),
			     (xpos + entry->labelLen
			      + horizontalAdjust
			      + BorderOf (entry)));
   if (entry->fieldWin == 0)
   {
      destroyCDKObject (entry);
      return (0);
   }
   keypad (entry->fieldWin, TRUE);

   /* Make the label win, if we need to. */
   if (label != 0)
   {
      entry->labelWin = subwin (entry->win, 1, entry->labelLen,
				ypos + TitleLinesOf (entry) + BorderOf (entry),
				xpos + horizontalAdjust + BorderOf (entry));
   }

   /* Make room for the info char * pointer. */
   entry->info = typeMallocN (char, max + 3);
   if (entry->info == 0)
   {
      destroyCDKObject (entry);
      return (0);
   }
   cleanChar (entry->info, max + 3, '\0');
   entry->infoWidth = max + 3;

   /* *INDENT-EQLS* Set up the rest of the structure. */
   ScreenOf (entry)             = cdkscreen;
   entry->parent                = cdkscreen->window;
   entry->shadowWin             = 0;
   entry->fieldAttr             = fieldAttr;
   entry->fieldWidth            = fieldWidth;
   entry->filler                = filler;
   entry->hidden                = filler;
   ObjOf (entry)->inputWindow   = entry->fieldWin;
   ObjOf (entry)->acceptsFocus  = TRUE;
   ReturnOf (entry)             = NULL;
   entry->shadow                = shadow;
   entry->screenCol             = 0;
   entry->leftChar              = 0;
   entry->min                   = min;
   entry->max                   = max;
   entry->boxWidth              = boxWidth;
   entry->boxHeight             = boxHeight;
   initExitType (entry);
   entry->dispType              = dispType;
   entry->callbackfn            = CDKEntryCallBack;

   /* Do we want a shadow? */
   if (shadow)
   {
      entry->shadowWin = newwin (
				   boxHeight,
				   boxWidth,
				   ypos + 1,
				   xpos + 1);
   }

   registerCDKObject (cdkscreen, vENTRY, entry);

   return (entry);
}

/*
 * This means you want to use the given entry field. It takes input
 * from the keyboard, and when its done, it fills the entry info
 * element of the structure with what was typed.
 */
char *activateCDKEntry (CDKENTRY *entry, chtype *actions)
{
   chtype input = 0;
   boolean functionKey;
   char *ret = 0;

   /* Draw the widget. */
   drawCDKEntry (entry, ObjOf (entry)->box);

   if (actions == 0)
   {
      for (;;)
      {
	 input = (chtype)getchCDKObject (ObjOf (entry), &functionKey);

	 /* Inject the character into the widget. */
	 ret = injectCDKEntry (entry, input);
	 if (entry->exitType != vEARLY_EXIT)
	 {
	    return ret;
	 }
      }
   }
   else
   {
      int length = chlen (actions);
      int x;

      /* Inject each character one at a time. */
      for (x = 0; x < length; x++)
      {
	 ret = injectCDKEntry (entry, actions[x]);
	 if (entry->exitType != vEARLY_EXIT)
	 {
	    return ret;
	 }
      }
   }

   /* Make sure we return the correct info. */
   if (entry->exitType == vNORMAL)
   {
      return entry->info;
   }
   else
   {
      return 0;
   }
}

static void setPositionToEnd (CDKENTRY *entry)
{
   int stringLen;

   stringLen = (int)utf8strlen (entry->info);
   if (stringLen >= entry->fieldWidth)
   {
      if (stringLen < entry->max)
      {
	 int charCount = entry->fieldWidth - 1;
	 entry->leftChar = stringLen - charCount; // horizontal scrolling with UTF8 does not work
	 entry->screenCol = charCount;
      }
      else
      {
	 entry->leftChar = stringLen - entry->fieldWidth;
	 entry->screenCol = stringLen - 1;
      }
   }
   else
   {
      entry->leftChar = 0;
      entry->screenCol = stringLen;
   }
}

static inline int isUtf8CharPart(unsigned int ch) { return (ch >= 0x80) && (ch < 0xc0); }

/*
 * This injects a single character into the widget.
 */
static int _injectCDKEntry (CDKOBJS *object, chtype input)
{
   CDKENTRY *widget = (CDKENTRY *)object;
   int ppReturn = 1;
   char *ret = unknownString;
   bool complete = FALSE;

   /* Set the exit type. */
   setExitType (widget, 0);

   /* Refresh the widget field. */
   drawCDKEntryField (widget);

   /* Check if there is a pre-process function to be called. */
   if (PreProcessFuncOf (widget) != 0)
   {
      ppReturn = PreProcessFuncOf (widget) (vENTRY,
					    widget,
					    PreProcessDataOf (widget),
					    input);
   }

   /* Should we continue? */
   if (ppReturn != 0)
   {
      /* Check a predefined binding... */
      if (checkCDKObjectBind (vENTRY, widget, input) != 0)
      {
	 checkEarlyExit (widget);
	 complete = TRUE;
      }
      else
      {
	 int infoLength = (int)strlen (widget->info);
	 int currPos = utf8charpos (widget->info, widget->screenCol) + widget->leftChar;

	 switch (input)
	 {
	 case KEY_UP:
	 case KEY_DOWN:
	    Beep ();
	    break;

	 case KEY_HOME:
	    widget->leftChar = 0;
	    widget->screenCol = 0;
	    drawCDKEntryField (widget);
	    break;

	 case CDK_TRANSPOSE:
	    if (currPos >= infoLength - 1)
	    {
	       Beep ();
	    }
	    else
	    {
	       char holder = widget->info[currPos];
	       widget->info[currPos] = widget->info[currPos + 1];
	       widget->info[currPos + 1] = holder;
	       drawCDKEntryField (widget);
	    }
	    break;

	 case KEY_END:
	    setPositionToEnd (widget);
	    drawCDKEntryField (widget);
	    break;

	 case KEY_LEFT:
	    if (currPos <= 0)
	    {
	       Beep ();
	    }
	    else if (widget->screenCol == 0)
	    {
	       /* Scroll left.  */
	       widget->leftChar--;
	       drawCDKEntryField (widget);
	    }
	    else
	    {
	       wmove (widget->fieldWin, 0, --widget->screenCol);
	       drawCDKEntryField (widget);
	    }
	    break;

	 case KEY_RIGHT:
	    if (currPos >= infoLength)
	    {
	       Beep ();
	    }
	    else if (widget->screenCol == widget->fieldWidth - 1)
	    {
	       /* Scroll to the right. */
	       widget->leftChar++;
	       drawCDKEntryField (widget);
	    }
	    else
	    {
	       /* Move right. */
	       wmove (widget->fieldWin, 0, ++widget->screenCol);
	       drawCDKEntryField (widget);
	    }
	    break;

	 case KEY_BACKSPACE:
	 case KEY_DC:
	    if (widget->dispType == vVIEWONLY)
	    {
	       Beep ();
	    }
	    else
	    {
	       bool success = FALSE;

	       if (input == KEY_BACKSPACE)
	       {
		  while (currPos >= 1 && isUtf8CharPart(CharOf(widget->info[currPos - 1]))) currPos--;
		  --currPos;
	       }

	       if (currPos >= 0 && infoLength > 0)
	       {
		  if (currPos < infoLength)
		  {
		     int x;

		     int len = utf8charlen(widget->info[currPos]);
		     for (x = currPos; x < infoLength; x++)
		     {
			widget->info[x] = widget->info[x + len];
		     }
		     success = TRUE;
		  }
		  else if (input == KEY_BACKSPACE)
		  {
		     while (infoLength >= 1 && isUtf8CharPart(CharOf(widget->info[infoLength - 1]))) infoLength--;
		     widget->info[infoLength - 1] = '\0';
		     success = TRUE;
		  }
	       }

	       if (success)
	       {
		  if (input == KEY_BACKSPACE)
		  {
		     if (widget->screenCol > 0)
			widget->screenCol--;
		     else
			widget->leftChar--;
		  }

		  drawCDKEntryField (widget);
	       }
	       else
	       {
		  Beep ();
	       }
	    }
	    break;

	 case KEY_ESC:
	    setExitType (widget, input);
	    complete = TRUE;
	    break;

	 case CDK_ERASE:
	    if (infoLength != 0)
	    {
	       cleanCDKEntry (widget);
	       drawCDKEntryField (widget);
	    }
	    break;

	 case CDK_CUT:
	    if (infoLength != 0)
	    {
	       freeChar (GPasteBuffer);
	       GPasteBuffer = copyChar (widget->info);
	       cleanCDKEntry (widget);
	       drawCDKEntryField (widget);
	    }
	    else
	    {
	       Beep ();
	    }
	    break;

	 case CDK_COPY:
	    if (infoLength != 0)
	    {
	       freeChar (GPasteBuffer);
	       GPasteBuffer = copyChar (widget->info);
	    }
	    else
	    {
	       Beep ();
	    }
	    break;

	 case CDK_PASTE:
	    if (GPasteBuffer != 0)
	    {
	       setCDKEntryValue (widget, GPasteBuffer);
	       drawCDKEntryField (widget);
	    }
	    else
	    {
	       Beep ();
	    }
	    break;

	 case KEY_TAB:
	 case KEY_ENTER:
	    if (infoLength >= widget->min)
	    {
	       setExitType (widget, input);
	       ret = (widget->info);
	       complete = TRUE;
	    }
	    else
	    {
	       Beep ();
	    }
	    break;

	 case KEY_ERROR:
	    setExitType (widget, input);
	    complete = TRUE;
	    break;

	 case CDK_REFRESH:
	    eraseCDKScreen (ScreenOf (widget));
	    refreshCDKScreen (ScreenOf (widget));
	    break;

	 default:
	    (widget->callbackfn) (widget, input);
	    break;
	 }
      }

      /* Should we do a post-process? */
      if (!complete && (PostProcessFuncOf (widget) != 0))
      {
	 PostProcessFuncOf (widget) (vENTRY,
				     widget,
				     PostProcessDataOf (widget),
				     input);
      }
   }

   if (!complete)
      setExitType (widget, 0);

   ResultOf (widget).valueString = ret;
   return (ret != unknownString);
}

/*
 * This moves the entry field to the given location.
 */
static void _moveCDKEntry (CDKOBJS *object,
			   int xplace,
			   int yplace,
			   boolean relative,
			   boolean refresh_flag)
{
   /* *INDENT-EQLS* */
   CDKENTRY *entry = (CDKENTRY *)object;
   int currentX    = getbegx (entry->win);
   int currentY    = getbegy (entry->win);
   int xpos        = xplace;
   int ypos        = yplace;
   int xdiff       = 0;
   int ydiff       = 0;

   /*
    * If this is a relative move, then we will adjust where we want
    * to move to.
    */
   if (relative)
   {
      xpos = getbegx (entry->win) + xplace;
      ypos = getbegy (entry->win) + yplace;
   }

   /* Adjust the window if we need to. */
   alignxy (WindowOf (entry), &xpos, &ypos, entry->boxWidth, entry->boxHeight);

   /* Get the difference. */
   xdiff = currentX - xpos;
   ydiff = currentY - ypos;

   /* Move the window to the new location. */
   moveCursesWindow (entry->win, -xdiff, -ydiff);
   moveCursesWindow (entry->fieldWin, -xdiff, -ydiff);
   moveCursesWindow (entry->labelWin, -xdiff, -ydiff);
   moveCursesWindow (entry->shadowWin, -xdiff, -ydiff);

   /* Touch the windows so they 'move'. */
   refreshCDKWindow (WindowOf (entry));

   /* Redraw the window, if they asked for it. */
   if (refresh_flag)
   {
      drawCDKEntry (entry, ObjOf (entry)->box);
   }
}

static void toutf8(int code, char *s, int *len) {
  int b;
  if (code < 0x80) *len = 1;
  else if (code < 0x800) *len = 2;
  else if (code < 0x10000) *len = 3;
  else if (code < 0x200000) *len = 4;
  else if (code < 0x4000000) *len = 5;
  else *len = 6;
  for (b = *len - 1; b > 0; b--) s[b] = 0x80 | (code & 0x3F), code >>= 6;
  if (*len == 1) s[0] = code & 0x7F;
  else if (*len == 2) s[0] = 0xC0 | (code & 0x1F);
  else if (*len == 3) s[0] = 0xE0 | (code & 0x0F);
  else if (*len == 4) s[0] = 0xF0 | (code & 0x07);
  else if (*len == 5) s[0] = 0xF8 | (code & 0x03);
  else if (*len == 6) s[0] = 0xFC | (code & 0x01);
}

/*
 * This is a generic character parser for the entry field. It is used as a
 * callback function, so any personal modifications can be made by creating
 * a new function and calling the activation with its name.
 */
static void CDKEntryCallBack (CDKENTRY *entry, chtype character)
{
   int plainchar = character;
   char utf8[6];
   int len;

   if (plainchar == ERR ||
       ((int)utf8strlen (entry->info) >= entry->max))
   {
      Beep ();
   }
   else
   {
      toutf8(plainchar, utf8, &len);
      /* Update the screen and pointer. */
      if (entry->screenCol != entry->fieldWidth - 1)
      {
	 int x;
	 int pos = utf8charpos (entry->info, entry->screenCol) + entry->leftChar;

	 for (x = (int)strlen (entry->info) + len - 1;
	      x > pos + len - 1;
	      x--)
	 {
	    entry->info[x] = entry->info[x - len];
	 }
	 strncpy(entry->info + pos, utf8, len);
	 entry->screenCol++;
      }
      else
      {
	 /* Update the character pointer. */
	 size_t temp = strlen (entry->info);
	 strncpy(entry->info + temp, utf8, len);
	 entry->info[temp + len] = '\0';
	 /* Do not update the pointer if it's the last character */
	 if ((int)(temp + len + 1) < entry->max)
	    entry->leftChar++;
      }

      /* Update the entry field. */
      drawCDKEntryField (entry);
   }
}

/*
 * This erases the information in the entry field
 * and redraws a clean and empty entry field.
 */
void cleanCDKEntry (CDKENTRY *entry)
{
   int width = entry->fieldWidth;

   /* Erase the information in the character pointer. */
   cleanChar (entry->info, entry->infoWidth, '\0');

   /* Clean the entry screen field. */
   (void)mvwhline (entry->fieldWin, 0, 0, entry->filler, width);

   /* Reset some variables. */
   entry->screenCol = 0;
   entry->leftChar = 0;

   /* Refresh the entry field. */
   wrefresh (entry->fieldWin);
}

/*
 * This draws the entry field.
 */
static void _drawCDKEntry (CDKOBJS *object, boolean Box)
{
   CDKENTRY *entry = (CDKENTRY *)object;

   /* Did we ask for a shadow? */
   if (entry->shadowWin != 0)
   {
      drawShadow (entry->shadowWin);
   }

   /* Box the widget if asked. */
   if (Box)
   {
      drawObjBox (entry->win, ObjOf (entry));
   }

   drawCdkTitle (entry->win, object);

   wrefresh (entry->win);

   /* Draw in the label to the widget. */
   if (entry->labelWin != 0)
   {
      writeChtype (entry->labelWin, 0, 0, entry->label, HORIZONTAL, 0, entry->labelLen);
      wrefresh (entry->labelWin);
   }

   drawCDKEntryField (entry);
}

/*
 * This redraws the entry field.
 */
static void drawCDKEntryField (CDKENTRY *entry)
{
   int x = 0;
   char *p;
   int charlen;

   /* Set background color and attributes of the entry field */
   wbkgd (entry->fieldWin, entry->fieldAttr);

   /* Draw in the filler characters. */
   (void)mvwhline (entry->fieldWin, 0, x, entry->filler | entry->fieldAttr, entry->fieldWidth);

   /* If there is information in the field. Then draw it in. */
   if (entry->info != 0)
   {
      int infoLength = (int)utf8strlen (entry->info);

      /* Redraw the field. */
      if (isHiddenDisplayType (entry->dispType))
      {
	 for (x = entry->leftChar; x < infoLength; x++)
	 {
	    (void)mvwaddch (entry->fieldWin, 0, x - entry->leftChar,
			    entry->hidden | entry->fieldAttr);
	 }
      }
      else
      {
	 p = entry->info + entry->leftChar;
	 for (x = entry->leftChar; x < infoLength; x++, p+=charlen)
	 {
	    charlen = utf8charlen(CharOf (*p));
	    (void)mvwaddnstr (entry->fieldWin, 0, x - entry->leftChar,
			    (const char *)p, charlen);
	 }
      }
      wmove (entry->fieldWin, 0, entry->screenCol);
   }

   wrefresh (entry->fieldWin);
}

/*
 * This erases an entry widget from the screen.
 */
static void _eraseCDKEntry (CDKOBJS *object)
{
   if (validCDKObject (object))
   {
      CDKENTRY *entry = (CDKENTRY *)object;

      eraseCursesWindow (entry->fieldWin);
      eraseCursesWindow (entry->labelWin);
      eraseCursesWindow (entry->win);
      eraseCursesWindow (entry->shadowWin);
   }
}

/*
 * This destroys an entry widget.
 */
static void _destroyCDKEntry (CDKOBJS *object)
{
   if (object != 0)
   {
      CDKENTRY *entry = (CDKENTRY *)object;

      cleanCdkTitle (object);
      freeChtype (entry->label);
      freeChar (entry->info);

      /* Delete the windows. */
      deleteCursesWindow (entry->fieldWin);
      deleteCursesWindow (entry->labelWin);
      deleteCursesWindow (entry->shadowWin);
      deleteCursesWindow (entry->win);

      /* Clean the key bindings. */
      cleanCDKObjectBindings (vENTRY, entry);

      /* Unregister this object. */
      unregisterCDKObject (vENTRY, entry);
   }
}

/*
 * This sets specific attributes of the entry field.
 */
void setCDKEntry (CDKENTRY *entry,
		  const char *value,
		  int min,
		  int max,
		  boolean Box GCC_UNUSED)
{
   setCDKEntryValue (entry, value);
   setCDKEntryMin (entry, min);
   setCDKEntryMax (entry, max);
}

/*
 * This removes the old information in the entry field and keeps the
 * new information given.
 */
void setCDKEntryValue (CDKENTRY *entry, const char *newValue)
{
   /* If the pointer sent in is the same pointer as before, do nothing. */
   if (entry->info != newValue)
   {
      /* Just to be sure, if lets make sure the new value isn't null. */
      if (newValue == 0)
      {
	 /* Then we want to just erase the old value. */
	 cleanChar (entry->info, entry->infoWidth, '\0');

	 /* Set the pointers back to zero. */
	 entry->leftChar = 0;
	 entry->screenCol = 0;
      }
      else
      {
	 /* Determine how many characters we need to copy. */
	 int copychars = MINIMUM ((int)strlen (newValue), entry->max);

	 /* OK, erase the old value, and copy in the new value. */
	 cleanChar (entry->info, entry->max, '\0');
	 strncpy (entry->info, newValue, (unsigned)copychars);

	 setPositionToEnd (entry);
      }
   }
}
char *getCDKEntryValue (CDKENTRY *entry)
{
   return entry->info;
}

/*
 * This sets the maximum length of the string that will be accepted.
 */
void setCDKEntryMax (CDKENTRY *entry, int max)
{
   entry->max = max;
}
int getCDKEntryMax (CDKENTRY *entry)
{
   return entry->max;
}

/*
 * This sets the minimum length of the string that will
 * be accepted.
 */
void setCDKEntryMin (CDKENTRY *entry, int min)
{
   entry->min = min;
}
int getCDKEntryMin (CDKENTRY *entry)
{
   return entry->min;
}

/*
 * This sets the filler character to be used in the entry field.
 */
void setCDKEntryFillerChar (CDKENTRY *entry, chtype fillerCharacter)
{
   entry->filler = fillerCharacter;
}
chtype getCDKEntryFillerChar (CDKENTRY *entry)
{
   return entry->filler;
}

/*
 * This sets the character to use when a hidden type is used.
 */
void setCDKEntryHiddenChar (CDKENTRY *entry, chtype hiddenCharacter)
{
   entry->hidden = hiddenCharacter;
}
chtype getCDKEntryHiddenChar (CDKENTRY *entry)
{
   return entry->hidden;
}

/*
 * This sets the widgets box attribute.
 */
void setCDKEntryBox (CDKENTRY *entry, boolean Box)
{
   ObjOf (entry)->box = Box;
   ObjOf (entry)->borderSize = Box ? 1 : 0;
}
boolean getCDKEntryBox (CDKENTRY *entry)
{
   return ObjOf (entry)->box;
}

/*
 * This sets the background attribute of the widget.
 */
static void _setBKattrEntry (CDKOBJS *object, chtype attrib)
{
   if (object != 0)
   {
      CDKENTRY *widget = (CDKENTRY *)object;

      wbkgd (widget->win, attrib);
      wbkgd (widget->fieldWin, attrib);
      if (widget->labelWin != 0)
      {
	 wbkgd (widget->labelWin, attrib);
      }
   }
}

/*
 * This sets the attribute of the entry field.
 */
void setCDKEntryHighlight (CDKENTRY *entry, chtype highlight, boolean cursor)
{
   wbkgd (entry->fieldWin, highlight);
   entry->fieldAttr = highlight;
   curs_set (cursor);
   /*
    *  FIXME -  if (cursor) { move the cursor to this widget }
    */
}

/*
 * This sets the entry field callback function.
 */
void setCDKEntryCB (CDKENTRY *entry, ENTRYCB callback)
{
   entry->callbackfn = callback;
}

static void _focusCDKEntry (CDKOBJS *object)
{
   CDKENTRY *entry = (CDKENTRY *)object;

   wmove (entry->fieldWin, 0, entry->screenCol);
   wrefresh (entry->fieldWin);
}

static void _unfocusCDKEntry (CDKOBJS *object)
{
   CDKENTRY *entry = (CDKENTRY *)object;

   drawCDKEntry (entry, ObjOf (entry)->box);
   wrefresh (entry->fieldWin);
}

#if 0
static void _refreshDataCDKEntry (CDKOBJS *object)
{
   CDKENTRY *entry = (CDKENTRY *)object;

   if (ReturnOf (entry))
   {
      switch (DataTypeOf (entry))
      {
      default:
      case DataTypeString:
	 strcpy (entry->info, (char *)ReturnOf (entry));
	 break;
      case DataTypeInt:
	 sprintf (entry->info, "%d", *((int *)ReturnOf (entry)));
	 break;
      case DataTypeFloat:
	 sprintf (entry->info, "%g", *((float *)ReturnOf (entry)));
	 break;
      case DataTypeDouble:
	 sprintf (entry->info, "%g", *((double *)ReturnOf (entry)));
	 break;
      }
      entry->screenCol = strlen (entry->info);
      drawCDKEntryField (entry);
   }
}

static void _saveDataCDKEntry (CDKOBJS *object)
{
   CDKENTRY *entry = (CDKENTRY *)object;

   if (ReturnOf (entry))
   {
      switch (DataTypeOf (entry))
      {
      default:
      case DataTypeString:
	 strcpy ((char *)ReturnOf (entry), entry->info);
	 break;
      case DataTypeInt:
	 *((int *)ReturnOf (entry)) = atoi (entry->info);
	 break;
      case DataTypeFloat:
	 *((float *)ReturnOf (entry)) = atof (entry->info);
	 break;
      case DataTypeDouble:
	 *((double *)ReturnOf (entry)) = atof (entry->info);
	 break;
      }
   }
}
#else
dummyRefreshData (Entry)
dummySaveData (Entry)
#endif
