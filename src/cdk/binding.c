#include <cdk_int.h>
#include "termkey.h"
extern TermKey *ta_tk;

/*
 * $Author: tom $
 * $Date: 2019/02/19 10:12:38 $
 * $Revision: 1.57 $
 *
 * Notes:
 *
 * The cdktype parameter passed to bindCDKObject, etc., is redundant since
 * the object parameter also has the same information.  For compatibility
 * just use it for a sanity check.
 */

#ifndef KEY_MAX
#define KEY_MAX 512
#endif

static CDKOBJS *bindableObject (EObjectType * cdktype, void *object)
{
   CDKOBJS *obj = (CDKOBJS *)object;

   if (obj != 0 && *cdktype == ObjTypeOf (obj))
   {
      if (*cdktype == vFSELECT)
      {
	 *cdktype = vENTRY;
	 object = ((CDKFSELECT *)object)->entryField;
      }
      else if (*cdktype == vALPHALIST)
      {
	 *cdktype = vENTRY;
	 object = ((CDKALPHALIST *)object)->entryField;
      }
   }
   else
   {
      object = 0;
   }
   return (CDKOBJS *)object;
}

/*
 * This inserts a binding.
 */
void bindCDKObject (EObjectType cdktype,
		    void *object,
		    chtype key,
		    BINDFN function,
		    void *data)
{
   CDKOBJS *obj = bindableObject (&cdktype, object);

   if ((key < KEY_MAX) && obj != 0)
   {
      if (key != 0 && (unsigned)key >= obj->bindingCount)
      {
	 unsigned next = (unsigned) (key + 1);

	 if (obj->bindingList != 0)
	    obj->bindingList = typeReallocN (CDKBINDING, obj->bindingList, next);
	 else
	    obj->bindingList = typeMallocN (CDKBINDING, next);

	 memset (&(obj->bindingList[obj->bindingCount]), 0,
		 (next - obj->bindingCount) * sizeof (CDKBINDING));
	 obj->bindingCount = next;
      }

      if (obj->bindingList != 0)
      {
	 obj->bindingList[key].bindFunction = function;
	 obj->bindingList[key].bindData = data;
      }
   }
}

/*
 * This removes a binding on an object.
 */
void unbindCDKObject (EObjectType cdktype, void *object, chtype key)
{
   CDKOBJS *obj = bindableObject (&cdktype, object);

   if (obj != 0 && ((unsigned)key < obj->bindingCount))
   {
      obj->bindingList[key].bindFunction = 0;
      obj->bindingList[key].bindData = 0;
   }
}

/*
 * This removes all the bindings for the given objects.
 */
void cleanCDKObjectBindings (EObjectType cdktype, void *object)
{
   CDKOBJS *obj = bindableObject (&cdktype, object);

   if (obj != 0 && obj->bindingList != 0)
   {
      unsigned x;

      for (x = 0; x < obj->bindingCount; x++)
      {
	 (obj)->bindingList[x].bindFunction = 0;
	 (obj)->bindingList[x].bindData = 0;
      }
      freeAndNull ((obj)->bindingList);
   }
}

/*
 * This checks to see if the binding for the key exists:
 * If it does then it runs the command and returns its value, normally TRUE.
 * If it doesn't it returns a FALSE.  This way we can 'overwrite' coded
 * bindings.
 */
int checkCDKObjectBind (EObjectType cdktype, void *object, chtype key)
{
   CDKOBJS *obj = bindableObject (&cdktype, object);

   if (obj != 0 && ((unsigned)key < obj->bindingCount))
   {
      if ((obj)->bindingList[key].bindFunction != 0)
      {
	 BINDFN function = obj->bindingList[key].bindFunction;
	 void *data = obj->bindingList[key].bindData;

	 return function (cdktype, object, data, key);
      }
   }
   return (FALSE);
}

/*
 * This checks to see if the binding for the key exists.
 */
bool isCDKObjectBind (EObjectType cdktype, void *object, chtype key)
{
   bool result = FALSE;
   CDKOBJS *obj = bindableObject (&cdktype, object);

   if (obj != 0 && ((unsigned)key < obj->bindingCount))
   {
      if ((obj)->bindingList[key].bindFunction != 0)
	 result = TRUE;
   }
   return (result);
}

/*
 * This is a dummy function used to ensure that the constant for mapping has
 * a distinct address.
 */
int getcCDKBind (EObjectType cdktype GCC_UNUSED,
		 void *object GCC_UNUSED,
		 void *clientData GCC_UNUSED,
		 chtype input GCC_UNUSED)
{
   return 0;
}

/*
 * Read from the input window, filtering keycodes as needed.
 */
int getcCDKObject (CDKOBJS *obj)
{
   EObjectType cdktype = ObjTypeOf (obj);
   CDKOBJS *test = bindableObject (&cdktype, obj);
   int result = ERR;
   TermKeyKey key;
   int keysyms[] = {0,KEY_BACKSPACE,KEY_TAB,KEY_ENTER,KEY_ESC,0,0,KEY_UP,KEY_DOWN,KEY_LEFT,KEY_RIGHT,0,0,KEY_IC,KEY_DC,0,KEY_PPAGE,KEY_NPAGE,KEY_HOME,KEY_END};
   TermKeyResult res;
retry:
   res = termkey_waitkey(ta_tk, &key);
   switch (res)
   {
   case TERMKEY_RES_KEY:
      switch (key.type)
      {
      case TERMKEY_TYPE_UNICODE:
         result = key.code.codepoint;
         if ((key.modifiers & TERMKEY_KEYMOD_CTRL) &&
             ((result >= 'a' && result <= 'z') || (result >= 'A' && result <= 'Z')))
         {
            result = (result & ~0x60);
            key.modifiers &= ~TERMKEY_KEYMOD_CTRL;
         }
         break;
      case TERMKEY_TYPE_FUNCTION:
         result = KEY_F(key.code.number);
         break;
      case TERMKEY_TYPE_KEYSYM:
         result = keysyms[key.code.sym];
         if (key.code.sym == TERMKEY_SYM_TAB && key.modifiers & TERMKEY_KEYMOD_SHIFT)
            result = KEY_BTAB;
         break;
      case TERMKEY_TYPE_UNKNOWN_CSI:
      case TERMKEY_TYPE_MOUSE:
         goto retry;
      default:
         result = ERR;
      }
      break;
   case TERMKEY_RES_EOF:
   case TERMKEY_RES_ERROR:
      result = ERR;
      break;
   case TERMKEY_RES_NONE:
   case TERMKEY_RES_AGAIN:
      result = 0;
      break;
   }

   if (result >= 0
       && test != 0
       && (unsigned)result < test->bindingCount
       && test->bindingList[result].bindFunction == getcCDKBind)
   {
      result = (int)(long)test->bindingList[result].bindData;
   }
   else if (test == 0
	    || (unsigned)result >= test->bindingCount
	    || test->bindingList[result].bindFunction == 0)
   {
      switch (result)
      {
      case '\r':
      case '\n':
	 result = KEY_ENTER;
	 break;
      case '\t':
	 result = KEY_TAB;
	 break;
      case DELETE:
	 result = KEY_DC;
	 break;
      case '\b':		/* same as CTRL('H'), for ASCII */
	 result = KEY_BACKSPACE;
	 break;
      case CDK_BEGOFLINE:
	 result = KEY_HOME;
	 break;
      case CDK_ENDOFLINE:
	 result = KEY_END;
	 break;
      case CDK_FORCHAR:
	 result = KEY_RIGHT;
	 break;
      case CDK_BACKCHAR:
	 result = KEY_LEFT;
	 break;
      case CDK_NEXT:
	 result = KEY_TAB;
	 break;
      case CDK_PREV:
	 result = KEY_BTAB;
	 break;
      }
   }
   return result;
}

/*
 * Use this function rather than getcCDKObject(), since we can extend it to
 * handle wide-characters.
 */
int getchCDKObject (CDKOBJS *obj, boolean *functionKey)
{
   int key = getcCDKObject (obj);
   if (functionKey != NULL)
   {
      *functionKey = (key >= KEY_MIN && key <= KEY_MAX);
   }
   return key;
}
