/*=========================================================================

  Program:   Visualization Toolkit
  Module:    vtkKWWidget.cxx
  Language:  C++
  Date:      $Date$
  Version:   $Revision$

Copyright (c) 2000-2001 Kitware Inc. 469 Clifton Corporate Parkway,
Clifton Park, NY, 12065, USA.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

 * Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.

 * Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

 * Neither the name of Kitware nor the names of any contributors may be used
   to endorse or promote products derived from this software without specific 
   prior written permission.

 * Modified source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS''
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

=========================================================================*/
#include "vtkKWWidget.h"

#include "vtkKWApplication.h"
#include "vtkKWTkUtilities.h"
#include "vtkKWWidgetCollection.h"
#include "vtkKWWindow.h"
#include "vtkLinkedList.txx"
#include "vtkLinkedListIterator.txx"
#include "vtkObjectFactory.h"
#include "vtkString.h"

//-----------------------------------------------------------------------------
vtkStandardNewMacro( vtkKWWidget );
vtkCxxRevisionMacro(vtkKWWidget, "1.66");

int vtkKWWidgetCommand(ClientData cd, Tcl_Interp *interp,
                       int argc, char *argv[]);

//-----------------------------------------------------------------------------
vtkKWWidget::vtkKWWidget()
{
  this->WidgetName               = NULL;
  this->Parent                   = NULL;
  this->CommandFunction          = vtkKWWidgetCommand;
  this->Children                 = vtkKWWidgetCollection::New();

  // Make tracking memory leaks easier.

  this->Children->Register(this);
  this->Children->Delete();
  this->DeletingChildren         = 0;
  this->BalloonHelpString        = NULL;  
  this->BalloonHelpJustification = 0;
  this->BalloonHelpInitialized   = 0;
  this->Enabled                  = 1;

  this->TraceName = NULL;

  // Drag and Drop

  this->DragAndDropTargets = NULL;
  this->EnableDragAndDrop = 0;
  this->DragAndDropAnchor = this;
}

//-----------------------------------------------------------------------------
vtkKWWidget::~vtkKWWidget()
{
  this->DragAndDropAnchor = NULL;
  if (this->DragAndDropTargets)
    {
    this->DeleteDragAndDropTargets();
    this->DragAndDropTargets->Delete();
    }

  if (this->BalloonHelpString)
    {
    this->SetBalloonHelpString(NULL);
    }
  this->Children->UnRegister(this);
  this->Children = NULL;
  
  if (this->Application)
    {
    this->Script("destroy %s",this->GetWidgetName());
    }
  if (this->WidgetName)
    {
    delete [] this->WidgetName;
    }
  this->SetParent(NULL);
  this->SetApplication(NULL);
  this->SetTraceName(NULL);
}

//-----------------------------------------------------------------------------
void vtkKWWidget::SetParent(vtkKWWidget *p)
{
  if (this->Parent && p)
    {
    vtkErrorMacro("Error attempt to reparent a widget!");
    return;
    }
  if (this->Parent)
    {
    vtkKWWidget *tmp = this->Parent;
    this->Parent = NULL;
    tmp->UnRegister(this);
    tmp->RemoveChild(this);
    }
  else if (p)
    {
    this->Parent = p;
    p->Register(this);
    p->AddChild(this);
    }
}

//-----------------------------------------------------------------------------
const char *vtkKWWidget::GetWidgetName()
{
  static unsigned long count = 0;

  // is the name is already set the just return it
  if (this->WidgetName)
    {
    return this->WidgetName;
    }

  // create this widgets name
  char local[256];
  // get the parents name
  if (this->Parent)
    {
    const char *tmp = this->Parent->GetWidgetName();
    sprintf(local,"%s.%lu",tmp,count);
    }
  else
    {
    sprintf(local,".%lu",count);
    }
  count++;
  this->WidgetName = new char [strlen(local)+1];
  strcpy(this->WidgetName,local);
  return this->WidgetName;
}

//-----------------------------------------------------------------------------
void vtkKWWidget::Create(vtkKWApplication *app, const char *name, 
                         const char *args)
{
  if (this->IsCreated())
    {
    vtkErrorMacro("widget already created");
    return;
    }

  this->SetApplication(app);

  this->Script("%s %s %s", name, this->GetWidgetName(), (args ? args : ""));

  this->UpdateEnableState();
}

//-----------------------------------------------------------------------------
void vtkKWWidget::SetUpBalloonHelpBindings()
{
  this->Script("bind %s <Enter> {+%s BalloonHelpTrigger %s}", 
               this->GetWidgetName(), this->Application->GetTclName(),
               this->GetTclName());
  this->Script("bind %s <ButtonPress> {+%s BalloonHelpWithdraw}", 
               this->GetWidgetName(), this->Application->GetTclName());
  this->Script("bind %s <KeyPress> {+%s BalloonHelpWithdraw}", 
               this->GetWidgetName(), this->Application->GetTclName());
  this->Script("bind %s <Leave> {+%s BalloonHelpCancel}", 
               this->GetWidgetName(), this->Application->GetTclName());
  this->Script("bind %s <B1-Motion> {+%s BalloonHelpWithdraw}", 
               this->GetWidgetName(), this->Application->GetTclName());  
}

//-----------------------------------------------------------------------------
int  vtkKWWidget::GetNetReferenceCount() 
{
  int childCounts = 0;
  vtkKWWidget *child;
  
  for (this->Children->InitTraversal(); 
       (child = this->Children->GetNextKWWidget());)
    {
    childCounts += child->GetNetReferenceCount();
    }
  return this->ReferenceCount + childCounts - 
    2*this->Children->GetNumberOfItems();
}

//-----------------------------------------------------------------------------
// Removing items in the middle of a traversal is a bad thing.
// UnRegister will handle removing all of the children.
void vtkKWWidget::RemoveChild(vtkKWWidget *w) 
{
  if ( ! this->DeletingChildren)
    {
    this->Children->RemoveItem(w);
    }
}


//-----------------------------------------------------------------------------
void vtkKWWidget::UnRegister(vtkObjectBase *o)
{
  if (!this->DeletingChildren)
    {
    // delete the children if we are about to be deleted
    if (this->ReferenceCount == this->Children->GetNumberOfItems() + 1)
      {
      vtkKWWidget *child;
  
      this->DeletingChildren = 1;
      this->Children->InitTraversal();
      while ((child = this->Children->GetNextKWWidget()))
        {
        child->SetParent(NULL);
        }
      this->Children->RemoveAllItems();
      this->DeletingChildren = 0;
      }
    }
  
  this->Superclass::UnRegister(o);
}

//-----------------------------------------------------------------------------
void vtkKWWidget::Focus()
{
  this->Script( "focus %s", this->GetWidgetName() );
}

//-----------------------------------------------------------------------------
void vtkKWWidget::SetBind(vtkKWObject* CalledObject, const char *Event, const char *CommandString)
{
  this->Script("bind %s %s { %s %s }", this->GetWidgetName(), 
               Event, CalledObject->GetTclName(), CommandString);
}

//-----------------------------------------------------------------------------
void vtkKWWidget::SetBind(const char *Event, const char *CommandString)
{
  this->Script("bind %s %s { %s }", this->GetWidgetName(), Event, CommandString);
}

//-----------------------------------------------------------------------------
void vtkKWWidget::SetBind(const char *event, const char *widget, const char *command)
{
  this->Script("bind %s %s { %s %s }", this->GetWidgetName(), event, widget, command);
}

//-----------------------------------------------------------------------------
void vtkKWWidget::SetBindAll(const char *event, const char *widget, const char *command)
{
  this->Script("bind all %s { %s %s }", event, widget, command);
}

//-----------------------------------------------------------------------------
void vtkKWWidget::SetBindAll(const char *event, const char *command)
{
  this->Script("bind all %s { %s }", event, command);
}

//-----------------------------------------------------------------------------
void vtkKWWidget::SetCommand(vtkKWObject* CalledObject, const char * CommandString)
{
  char* command = this->CreateCommand(CalledObject, CommandString);
  this->Application->SimpleScript(command);
  delete [] command;
}

//-----------------------------------------------------------------------------
char* vtkKWWidget::CreateCommand(vtkKWObject* CalledObject, const char * CommandString)
{
  ostrstream event;
  event << this->GetWidgetName() << " configure -command {" 
        << CalledObject->GetTclName() 
        << " " << CommandString << "} " << ends;

  return event.str();
}

//-----------------------------------------------------------------------------
void vtkKWWidget::SetBalloonHelpString(const char *str)
{
//    if (this->Application == NULL)
//      {
//      vtkErrorMacro("Application needs to be set before balloon help.");
//      return;
//      }

  // A little overkill.
  if (this->BalloonHelpString == NULL && str == NULL)
    {
    return;
    }

  // Normal string stuff.
  if (this->BalloonHelpString)
    {
    delete [] this->BalloonHelpString;
    this->BalloonHelpString = NULL;
    }
  if (str != NULL)
    {
    this->BalloonHelpString = new char[strlen(str)+1];
    strcpy(this->BalloonHelpString, str);
    }
  
  if ( this->BalloonHelpString && this->Application && 
       !this->BalloonHelpInitialized )
    {
    this->SetUpBalloonHelpBindings();
    this->BalloonHelpInitialized = 1;
    }
}

//-----------------------------------------------------------------------------
void vtkKWWidget::SerializeRevision(ostream& os, vtkIndent indent)
{
  this->Superclass::SerializeRevision(os,indent);
  os << indent << "vtkKWWidget ";
  this->ExtractRevision(os,"$Revision: 1.66 $");
}

//-----------------------------------------------------------------------------
vtkKWWindow* vtkKWWidget::GetWindow()
{
  vtkKWWindow* win =0;
  vtkKWWidget* widget = this->GetParent();
  while(widget)
    {
    if((win = vtkKWWindow::SafeDownCast(widget)))
      {
      return win;
      }
    widget = widget->GetParent();
    }
  return win;
}

//-----------------------------------------------------------------------------
vtkKWWidget *vtkKWWidget::GetChildWidgetWithName(const char *name)
{
  if (name)
    {
    vtkKWWidget *child;
    this->Children->InitTraversal();
    while ((child = this->Children->GetNextKWWidget()))
      {
      const char *wname = child->GetWidgetName();
      if (wname && !strcmp(wname, name))
        {
        return child;
        }
      }
    }

  return NULL;
}
       
//-----------------------------------------------------------------------------
// Methods for tracing.

vtkKWWidget *vtkKWWidget::GetChildWidgetWithTraceName(const char *traceName)
{
  vtkKWWidget *child;

  this->Children->InitTraversal();
  while ( (child = this->Children->GetNextKWWidget()) )
    {
    if (child->GetTraceName())
      {
      if (strcmp(traceName, child->GetTraceName()) == 0)
        {
        return child;
        }
      }
    }
  return NULL;
}
       

//-----------------------------------------------------------------------------
int vtkKWWidget::InitializeTrace(ofstream* file)
{
  int dummyInit = 0;
  int *pInit;

  if(this->Application == NULL)
    {
    vtkErrorMacro("Tracing before widget is created.");
    return 0;
    }

  if (file == NULL || file == this->Application->GetTraceFile())
    { // Use the global trace file and respect initialized.
    file = this->Application->GetTraceFile();
    pInit = &(this->TraceInitialized);
    }
  else
    { // Use stateFile and ignore "TraceInitialized"
    pInit = &(dummyInit);
    }

  // There is no need to do anything if there is no trace file.
  if(file == NULL)
    {
    return 0;
    }
  if (*pInit)
    {
    return 1;
    }

  // The new general way to initialize objects (from vtkKWObject).
  if (this->TraceReferenceObject && this->TraceReferenceCommand)
    {
    if (this->TraceReferenceObject->InitializeTrace(file))
      {
      *file << "set kw(" << this->GetTclName() << ") "
            << "[$kw(" << this->TraceReferenceObject->GetTclName()
            << ") " << this->TraceReferenceCommand << "]" << endl;
      *pInit = 1;
      return 1;
      }
    }

  // I used to have the option of getting the child from the parent,
  // but it was not used, and we can simply set the 
  // TraceReferenceObject to the parent, and the TraceReferenceCommand to
  // GetChildWidgetName "this->TraceName".

  return 0;
}  

//-----------------------------------------------------------------------------
const char* vtkKWWidget::GetType()
{
  if ( this->Application )
    {
    this->Script("winfo class %s", this->GetWidgetName());
    return this->Application->GetMainInterp()->result;
    }
  return "None";
}

//-----------------------------------------------------------------------------
void vtkKWWidget::SetEnabled(int e)
{
  if ( this->Enabled == e )
    {
    return;
    }
  this->Enabled = e;

  this->UpdateEnableState();
  this->Modified();
}

//-----------------------------------------------------------------------------
void vtkKWWidget::UpdateEnableState()
{
  if (this->IsAlive() && this->HasConfigurationOption("-state"))
    {
    this->Script("%s configure -state %s", 
                 this->GetWidgetName(), 
                 (this->Enabled ? "normal" : "disabled"));
    }
}

//-----------------------------------------------------------------------------
void vtkKWWidget::GetBackgroundColor(int *r, int *g, int *b)
{
  vtkKWTkUtilities::GetBackgroundColor(this->Application->GetMainInterp(),
                                       this->GetWidgetName(), 
                                       r, g, b);
}

//-----------------------------------------------------------------------------
void vtkKWWidget::SetBackgroundColor(int r, int g, int b)
{
  char color[10];
  sprintf(color, "#%02x%02x%02x", r, g, b);
  this->Script("%s config -bg %s", this->GetWidgetName(), color);
}

//-----------------------------------------------------------------------------
void vtkKWWidget::GetBackgroundColor(float *r, float *g, float *b)
{
  int ir, ig, ib;
  this->GetBackgroundColor(&ir, &ig, &ib);

  *r = (float)ir / 255.0;
  *g = (float)ig / 255.0;
  *b = (float)ib / 255.0;
}

//-----------------------------------------------------------------------------
void vtkKWWidget::SetBackgroundColor(float r, float g, float b)
{
  char color[10];
  sprintf(color, "#%02x%02x%02x", 
          (int)(r * 255.0), (int)(g * 255.0), (int)(b * 255.0));
  this->Script("%s config -bg %s", this->GetWidgetName(), color);
}

//-----------------------------------------------------------------------------
void vtkKWWidget::GetForegroundColor(int *r, int *g, int *b)
{
  vtkKWTkUtilities::GetOptionColor(this->Application->GetMainInterp(),
                                   this->GetWidgetName(), 
                                   "-fg",
                                   r, g, b);
}

//-----------------------------------------------------------------------------
void vtkKWWidget::SetForegroundColor(int r, int g, int b)
{
  char color[10];
  sprintf(color, "#%02x%02x%02x", r, g, b);
  this->Script("%s config -fg %s", this->GetWidgetName(), color);
}

//-----------------------------------------------------------------------------
void vtkKWWidget::GetForegroundColor(float *r, float *g, float *b)
{
  int ir, ig, ib;
  this->GetForegroundColor(&ir, &ig, &ib);

  *r = (float)ir / 255.0;
  *g = (float)ig / 255.0;
  *b = (float)ib / 255.0;
}

//-----------------------------------------------------------------------------
void vtkKWWidget::SetForegroundColor(float r, float g, float b)
{
  char color[10];
  sprintf(color, "#%02x%02x%02x", 
          (int)(r * 255.0), (int)(g * 255.0), (int)(b * 255.0));
  this->Script("%s config -fg %s", this->GetWidgetName(), color);
}

//-----------------------------------------------------------------------------
int vtkKWWidget::HasConfigurationOption(const char* option)
{
  return (this->Application && 
          !this->Application->EvaluateBooleanExpression(
            "catch {%s cget %s}",
            this->GetWidgetName(), option));
}

//-----------------------------------------------------------------------------
int vtkKWWidget::GetConfigurationOptionAsInt(const char* option)
{
  if (!this->HasConfigurationOption(option))
    {
    return 0;
    }

  return atoi(this->Script("%s cget %s", this->GetWidgetName(), option));
}

//-----------------------------------------------------------------------------
int vtkKWWidget::IsAlive()
{
  if (!this->IsCreated())
    {
    return 0;
    }

  return atoi(this->Script("winfo exists %s", this->GetWidgetName()));
}

//-----------------------------------------------------------------------------
int vtkKWWidget::IsMapped()
{
  return this->IsAlive() && this->Application->EvaluateBooleanExpression(
    "winfo ismapped %s", this->GetWidgetName());
}

//-----------------------------------------------------------------------------
int vtkKWWidget::IsPacked()
{
  return this->IsCreated() && !this->Application->EvaluateBooleanExpression(
    "catch {pack info %s}", this->GetWidgetName());
}

//-----------------------------------------------------------------------------
int vtkKWWidget::GetNumberOfPackedChildren()
{
  if (!this->IsCreated())
    {
    return 0;
    }
  return atoi(this->Script("llength [pack slaves %s]", this->GetWidgetName()));
}

//-----------------------------------------------------------------------------
void vtkKWWidget::Unpack()
{
  this->Script("catch {eval pack forget %s}",
               this->GetWidgetName());
}

//-----------------------------------------------------------------------------
void vtkKWWidget::UnpackSiblings()
{
  if (this->GetParent())
    {
    this->Script("catch {eval pack forget [pack slaves %s]} \n "
                 "catch {eval grid forget [grid slaves %s]}",
                 this->GetParent()->GetWidgetName(),
                 this->GetParent()->GetWidgetName());
    }
}

//-----------------------------------------------------------------------------
void vtkKWWidget::UnpackChildren()
{
  this->Script("catch {eval pack forget [pack slaves %s]} \n "
               "catch {eval grid forget [grid slaves %s]}",
               this->GetWidgetName(),this->GetWidgetName());
}

//-----------------------------------------------------------------------------
void vtkKWWidget::AddChild(vtkKWWidget *w) 
{
  this->Children->AddItem(w);
}

//-----------------------------------------------------------------------------
void vtkKWWidget::SetEnableDragAndDrop(int arg)
{
  if (this->EnableDragAndDrop == arg)
    {
    return;
    }

  this->EnableDragAndDrop = arg;
  this->Modified();

  if (arg)
    {
    this->SetDragAndDropBindings();
    }
  else
    {
    this->RemoveDragAndDropBindings();
    }
}

//-----------------------------------------------------------------------------
void vtkKWWidget::SetDragAndDropAnchor(vtkKWWidget *arg)
{
  if (this->DragAndDropAnchor == arg)
    {
    return;
    }

  this->RemoveDragAndDropBindings();

  this->DragAndDropAnchor = arg;
  this->Modified();

  this->SetDragAndDropBindings();
}

//-----------------------------------------------------------------------------
void vtkKWWidget::SetDragAndDropBindings()
{
  if (!this->DragAndDropAnchor || !this->DragAndDropAnchor->IsCreated())
    {
    return;
    }
  
  this->Script("bind %s <Button-1> {+ %s DragAndDropStartCallback %%X %%Y}",
               this->DragAndDropAnchor->GetWidgetName(), this->GetTclName());
  this->Script("bind %s <B1-Motion> {+ %s DragAndDropPerformCallback %%X %%Y}",
               this->DragAndDropAnchor->GetWidgetName(), this->GetTclName());
  this->Script("bind %s <ButtonRelease-1> {+ %s DragAndDropEndCallback %%X %%Y}",
               this->DragAndDropAnchor->GetWidgetName(), this->GetTclName());
}

//-----------------------------------------------------------------------------
void vtkKWWidget::RemoveDragAndDropBindings()
{
  if (!this->DragAndDropAnchor || !this->DragAndDropAnchor->IsCreated())
    {
    return;
    }

  this->Script("bind %s <Button-1> {}",
               this->DragAndDropAnchor->GetWidgetName(), this->GetTclName());
  this->Script("bind %s <B1-Motion> {}",
               this->DragAndDropAnchor->GetWidgetName(), this->GetTclName());
  this->Script("bind %s <ButtonRelease-1> {}",
               this->DragAndDropAnchor->GetWidgetName(), this->GetTclName());
}

//-----------------------------------------------------------------------------
vtkKWWidget::DragAndDropTarget::DragAndDropTarget()
{
  this->Target = 0;
  this->StartCommand = 0;
  this->PerformCommand = 0;
  this->EndCommand = 0;
}

//-----------------------------------------------------------------------------
vtkKWWidget::DragAndDropTarget::~DragAndDropTarget()
{
  this->Target = 0;
  this->SetStartCommand(0);
  this->SetPerformCommand(0);
  this->SetEndCommand(0);
}

//-----------------------------------------------------------------------------
void vtkKWWidget::DragAndDropTarget::SetStartCommand(const char *arg)
{
  if ((this->StartCommand == NULL && arg == NULL) ||
      (this->StartCommand && arg && (!strcmp(this->StartCommand,arg))))
    { 
    return;
    }

  if (this->StartCommand) 
    { 
    delete [] this->StartCommand; 
    }

  if (arg)
    {
    this->StartCommand = new char[strlen(arg) + 1];
    strcpy(this->StartCommand, arg);
    }
   else
    {
    this->StartCommand = NULL;
    }
}

//-----------------------------------------------------------------------------
void vtkKWWidget::DragAndDropTarget::SetPerformCommand(const char *arg)
{
  if ((this->PerformCommand == NULL && arg == NULL) ||
      (this->PerformCommand && arg && (!strcmp(this->PerformCommand,arg))))
    { 
    return;
    }

  if (this->PerformCommand) 
    { 
    delete [] this->PerformCommand; 
    }

  if (arg)
    {
    this->PerformCommand = new char[strlen(arg) + 1];
    strcpy(this->PerformCommand, arg);
    }
   else
    {
    this->PerformCommand = NULL;
    }
}

//-----------------------------------------------------------------------------
void vtkKWWidget::DragAndDropTarget::SetEndCommand(const char *arg)
{
  if ((this->EndCommand == NULL && arg == NULL) ||
      (this->EndCommand && arg && (!strcmp(this->EndCommand,arg))))
    { 
    return;
    }

  if (this->EndCommand) 
    { 
    delete [] this->EndCommand; 
    }

  if (arg)
    {
    this->EndCommand = new char[strlen(arg) + 1];
    strcpy(this->EndCommand, arg);
    }
   else
    {
    this->EndCommand = NULL;
    }
}

//-----------------------------------------------------------------------------
void vtkKWWidget::DeleteDragAndDropTargets()
{
  if (!this->DragAndDropTargets)
    {
    return;
    }

  vtkKWWidget::DragAndDropTarget *target = NULL;
  vtkKWWidget::DragAndDropTargetsContainerIterator *it = 
    this->DragAndDropTargets->NewIterator();

  it->InitTraversal();
  while (!it->IsDoneWithTraversal())
    {
    if (it->GetData(target) == VTK_OK)
      {
      delete target;
      }
    it->GoToNextItem();
    }
  it->Delete();
}

//-----------------------------------------------------------------------------
vtkKWWidget::DragAndDropTarget*
vtkKWWidget::GetDragAndDropTarget(vtkKWWidget *widget)
{
  if (!this->DragAndDropTargets)
    {
    return NULL;
    }

  vtkKWWidget::DragAndDropTarget *target = NULL;
  vtkKWWidget::DragAndDropTarget *found = NULL;
  vtkKWWidget::DragAndDropTargetsContainerIterator *it = 
    this->DragAndDropTargets->NewIterator();

  it->InitTraversal();
  while (!it->IsDoneWithTraversal())
    {
    if (it->GetData(target) == VTK_OK && target->Target == widget)
      {
      found = target;
      break;
      }
    it->GoToNextItem();
    }
  it->Delete();

  return found;
}

//-----------------------------------------------------------------------------
int vtkKWWidget::AddDragAndDropTarget(vtkKWWidget *widget)
{
  vtkKWWidget::DragAndDropTarget *found = this->GetDragAndDropTarget(widget);
  if (found)
    {
    vtkErrorMacro("The Drag & Drop target already exists.");
    return 0;
    }

  if (!this->DragAndDropTargets)
    {
    this->DragAndDropTargets = vtkKWWidget::DragAndDropTargetsContainer::New();
    }

  vtkKWWidget::DragAndDropTarget *target = new vtkKWWidget::DragAndDropTarget;
  if (this->DragAndDropTargets->AppendItem(target) != VTK_OK)
    {
    vtkErrorMacro("Error while adding a Drag & Drop target to the widget.");
    delete target;
    return 0;
    }

  target->Target = widget;

  return 1;
}

//-----------------------------------------------------------------------------
int vtkKWWidget::RemoveDragAndDropTarget(vtkKWWidget *widget)
{
  vtkKWWidget::DragAndDropTarget *found = this->GetDragAndDropTarget(widget);
  if (!found)
    {
    return 0;
    }
  
  vtkIdType idx = 0;
  if (this->DragAndDropTargets->FindItem(found, idx) != VTK_OK)
    {
    vtkErrorMacro("Error while searching for a Drag & Drop target.");
    return 0;
    }

  if (this->DragAndDropTargets->RemoveItem(idx) != VTK_OK)
    {
    vtkErrorMacro("Error while removing a Drag & Drop target.");
    return 0;
    }

  delete found;

  return 1;
}

//-----------------------------------------------------------------------------
int vtkKWWidget::HasDragAndDropTarget(vtkKWWidget *widget)
{
  vtkKWWidget::DragAndDropTarget *found = this->GetDragAndDropTarget(widget);
  return found ? 1 : 0;
}

//-----------------------------------------------------------------------------
int vtkKWWidget::GetNumberOfDragAndDropTargets()
{
  if (!this->DragAndDropTargets)
    {
    return 0;
    }

  return this->DragAndDropTargets->GetNumberOfItems();
}

//-----------------------------------------------------------------------------
int vtkKWWidget::SetDragAndDropStartCommand(vtkKWWidget *target, 
                                            vtkKWObject *object, 
                                            const char *method)
{
  if (!target || !object || !method || !method[0])
    {
    return 0;
    }

  vtkKWWidget::DragAndDropTarget *found = this->GetDragAndDropTarget(target);
  if (!found)
    {
    return 0;
    }

  ostrstream command;
  command << object->GetTclName() << " " << method << ends;
  found->SetStartCommand(command.str());
  command.rdbuf()->freeze(0);

  return 1;
}

//-----------------------------------------------------------------------------
int vtkKWWidget::SetDragAndDropPerformCommand(vtkKWWidget *target, 
                                              vtkKWObject *object, 
                                              const char *method)
{
  if (!target || !object || !method || !method[0])
    {
    return 0;
    }

  vtkKWWidget::DragAndDropTarget *found = this->GetDragAndDropTarget(target);
  if (!found)
    {
    return 0;
    }

  ostrstream command;
  command << object->GetTclName() << " " << method << ends;
  found->SetPerformCommand(command.str());
  command.rdbuf()->freeze(0);

  return 1;
}

//-----------------------------------------------------------------------------
int vtkKWWidget::SetDragAndDropEndCommand(vtkKWWidget *target, 
                                          vtkKWObject *object, 
                                          const char *method)
{
  if (!target || !object || !method || !method[0])
    {
    return 0;
    }

  vtkKWWidget::DragAndDropTarget *found = this->GetDragAndDropTarget(target);
  if (!found)
    {
    return 0;
    }

  ostrstream command;
  command << object->GetTclName() << " " << method << ends;
  found->SetEndCommand(command.str());
  command.rdbuf()->freeze(0);

  return 1;
}

//-----------------------------------------------------------------------------
void vtkKWWidget::DragAndDropStartCallback(int x, int y)
{
  if (!this->EnableDragAndDrop || 
      !this->DragAndDropTargets || 
      !this->GetNumberOfDragAndDropTargets())
    {
    return;
    }

  // Set the cursor and invert foreground/background to better show what
  // is dragged

  if (this->DragAndDropAnchor && this->DragAndDropAnchor->IsCreated())
    {
    this->Script("[winfo toplevel %s] config -cursor hand2", 
                 this->DragAndDropAnchor->GetWidgetName());

    if (this->DragAndDropAnchor->HasConfigurationOption("-fg") &&
        this->DragAndDropAnchor->HasConfigurationOption("-bg"))
      {
      int fr, fg, fb, br, bg, bb;
      this->DragAndDropAnchor->GetForegroundColor(&fr, &fg, &fb);
      this->DragAndDropAnchor->GetBackgroundColor(&br, &bg, &bb);
      this->DragAndDropAnchor->SetForegroundColor(br, bg, bb);
      this->DragAndDropAnchor->SetBackgroundColor(fr, fg, fb);
      }
    }

  // Call each target's StartCommand

  vtkKWWidget::DragAndDropTarget *target = NULL;
  vtkKWWidget::DragAndDropTargetsContainerIterator *it = 
    this->DragAndDropTargets->NewIterator();

  it->InitTraversal();
  while (!it->IsDoneWithTraversal())
    {
    if (it->GetData(target) == VTK_OK && target->StartCommand)
      {
      this->Script("eval %s %d %d %s %s", 
                   target->StartCommand, x, y, 
                   this->GetTclName(), this->DragAndDropAnchor->GetTclName());
      }
    it->GoToNextItem();
    }
  it->Delete();
}

//-----------------------------------------------------------------------------
void vtkKWWidget::DragAndDropPerformCallback(int x, int y)
{
  if (!this->EnableDragAndDrop || 
      !this->DragAndDropTargets || 
      !this->GetNumberOfDragAndDropTargets())
    {
    return;
    }

  // Call each target's PerformCommand

  vtkKWWidget::DragAndDropTarget *target = NULL;
  vtkKWWidget::DragAndDropTargetsContainerIterator *it = 
    this->DragAndDropTargets->NewIterator();
  
  it->InitTraversal();
  while (!it->IsDoneWithTraversal())
    {
    if (it->GetData(target) == VTK_OK && target->PerformCommand)
      {
      this->Script("eval %s %d %d %s %s", 
                   target->PerformCommand, x, y, 
                   this->GetTclName(), this->DragAndDropAnchor->GetTclName());
      }
    it->GoToNextItem();
    }
  it->Delete();
}

//-----------------------------------------------------------------------------
void vtkKWWidget::DragAndDropEndCallback(int x, int y)
{
  if (!this->EnableDragAndDrop || 
      !this->DragAndDropTargets || 
      !this->GetNumberOfDragAndDropTargets())
    {
    return;
    }

  // Reset the cursor and the background/foreground colors

  if (this->DragAndDropAnchor && this->DragAndDropAnchor->IsCreated())
    {
    this->Script("[winfo toplevel %s] config -cursor {}", 
                 this->DragAndDropAnchor->GetWidgetName());

    if (this->DragAndDropAnchor->HasConfigurationOption("-fg") &&
        this->DragAndDropAnchor->HasConfigurationOption("-bg"))
      {
      int fr, fg, fb, br, bg, bb;
      this->DragAndDropAnchor->GetForegroundColor(&fr, &fg, &fb);
      this->DragAndDropAnchor->GetBackgroundColor(&br, &bg, &bb);
      this->DragAndDropAnchor->SetForegroundColor(br, bg, bb);
      this->DragAndDropAnchor->SetBackgroundColor(fr, fg, fb);
      }
    }

  // Find if the cursor is in a target, and call its EndCommand

  vtkKWWidget::DragAndDropTarget *target = NULL;
  vtkKWWidget::DragAndDropTargetsContainerIterator *it = 
    this->DragAndDropTargets->NewIterator();
  
  it->InitTraversal();
  while (!it->IsDoneWithTraversal())
    {
    if (it->GetData(target) == VTK_OK && target->EndCommand && 
        target->Target && 
        target->Target->IsCreated() &&
        vtkKWTkUtilities::ContainsCoordinates(
          target->Target->GetApplication()->GetMainInterp(),
          target->Target->GetWidgetName(),
          x, y))
      {
      this->Script("eval %s %d %d %s %s %s", 
                   target->EndCommand, x, y, 
                   this->GetTclName(), this->DragAndDropAnchor->GetTclName(),
                   target->Target->GetTclName());
      }
    it->GoToNextItem();
    }
  it->Delete();
}

//-----------------------------------------------------------------------------
void vtkKWWidget::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os,indent);
  os << indent << "BalloonHelpJustification: " 
     << this->GetBalloonHelpJustification() << endl;
  os << indent << "BalloonHelpString: " 
     << (this->BalloonHelpString ? this->BalloonHelpString : "none") << endl;
  os << indent << "Children: " << this->GetChildren() << endl;
  os << indent << "Parent: " << this->GetParent() << endl;
  os << indent << "TraceName: " << (this->TraceName?this->TraceName:"none") 
     << endl;
  os << indent << "Enabled: " << (this->Enabled ? "on" : "off") << endl;
  os << indent << "EnableDragAndDrop: " 
     << (this->EnableDragAndDrop ? "On" : "Off") << endl;
  os << indent << "DragAndDropAnchor: " << this->DragAndDropAnchor << endl;
}




